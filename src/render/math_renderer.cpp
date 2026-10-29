#include "render/math_renderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable: 4099 4100 4996)
#include "core/formula.h"
#include "latex.h"
#include "render.h"
#include "utils/enums.h"
#include "utils/utf.h"
#pragma warning(pop)
#include "include/core/SkCanvas.h"
#include "render/microtex_skia_adapter.h"
#include "util/utf8.h"

namespace mdviewer {

struct MathRenderData {
    std::unique_ptr<tex::TeXRender> render;
};

namespace {

constexpr size_t kMaximumMathSourceBytes = 8192;
constexpr size_t kMaximumMathBraceDepth = 32;
constexpr size_t kMaximumMathOperations = 256;
constexpr size_t kMaximumMathEnvironments = 16;
std::mutex g_mathMutex;
std::filesystem::path g_resourceRoot;
bool g_initialized = false;
bool g_initializationFailed = false;

bool IsAsciiLetter(char character) {
    return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z');
}

bool IsInMathList(std::string_view list, std::string_view name) {
    // Whole tokens only; a prefix such as "new" must never authorize a command.
    size_t position = 0;
    while (position < list.size()) {
        const size_t end = list.find(' ', position);
        const auto token = list.substr(position, end == std::string_view::npos
            ? list.size() - position : end - position);
        if (token == name) return true;
        if (end == std::string_view::npos) break;
        position = end + 1;
    }
    return false;
}

// MicroTeX implements a macro language, including global definitions, numeric
// allocation/expansion commands and mutable font settings. Treat Markdown math
// as a bounded display language instead. Unknown commands fail closed, including
// aliases such as DeclareMathOperator and internal commands containing '@'.
bool PrepareSafeFormula(std::string_view source, std::string& normalized) {
    if (source.empty() || source.size() > kMaximumMathSourceBytes ||
        source.find('\0') != std::string_view::npos) {
        return false;
    }
    // MicroTeX's UTF-8 decoder is permissive: an overlong backslash encoding
    // could otherwise bypass control-sequence validation and become a command.
    if (SanitizeUtf8(source).replacementCount != 0) return false;

    // Strip comments before validation AND rendering. Preserve a separator so a
    // comment cannot concatenate fragments into a different control sequence.
    normalized.clear();
    normalized.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        const char character = source[i];
        if (character == '\\' && i + 1 < source.size()) {
            normalized += character;
            normalized += source[++i];
        } else if (character == '%') {
            while (i + 1 < source.size() && source[i + 1] != '\n' && source[i + 1] != '\r') ++i;
            normalized += '\n';
        } else {
            normalized += character;
        }
    }

    constexpr std::string_view commands =
        "frac dfrac tfrac sfrac binom dbinom tbinom sqrt sqrtsign "
        "text textrm textbf textit textsf texttt mbox operatorname "
        "mathrm mathbf mathit mathsf mathtt mathbb mathcal mathscr mathfrak mathds boldsymbol "
        "hat widehat tilde widetilde acute grave ddot mathring bar breve check vec dot dddot ddddot "
        "overline underline overbrace underbrace overrightarrow overleftarrow overleftrightarrow "
        "underrightarrow underleftarrow underleftrightarrow overset underset stackrel "
        "boxed phantom hphantom vphantom cancel bcancel xcancel "
        "left right middle big Big bigg Bigg bigl Bigl biggl Biggl bigr Bigr biggr Biggr "
        "mathop mathord mathrel mathbin mathopen mathclose mathpunct mathinner "
        "limits nolimits displaystyle textstyle scriptstyle scriptscriptstyle "
        "quad qquad thinspace medspace thickspace negthinspace negmedspace negthickspace "
        "sin cos tan cot sec csc arcsin arccos arctan sinh cosh tanh coth "
        "log ln lg exp lim limsup liminf min max inf sup det dim gcd ker hom arg Pr "
        "mod pmod pod substack "
        "alpha beta gamma delta epsilon varepsilon zeta eta theta vartheta iota kappa varkappa "
        "lambda mu nu xi omicron pi varpi rho varrho sigma varsigma tau upsilon phi varphi chi psi omega "
        "Gamma Delta Theta Lambda Xi Pi Sigma Upsilon Phi Psi Omega "
        "sum prod coprod int iint iiint iiiint idotsint oint bigcup bigcap bigvee bigwedge "
        "bigoplus bigotimes bigodot biguplus bigsqcup "
        "infty partial nabla hbar ell Re Im aleph beth gimel daleth wp imath jmath "
        "forall exists nexists neg lnot emptyset varnothing top bot angle measuredangle triangle "
        "pm mp times div cdot ast star circ bullet oplus ominus otimes oslash odot cap cup "
        "sqcap sqcup uplus vee wedge lor land setminus smallsetminus wr amalg "
        "le leq ge geq ne neq equiv sim simeq approx asymp cong propto prec succ preceq succeq "
        "ll gg in ni notin subset supset subseteq supseteq subsetneq supsetneq "
        "sqsubset sqsupset sqsubseteq sqsupseteq parallel nparallel perp mid nmid models vdash dashv "
        "to gets leftarrow rightarrow leftrightarrow Leftarrow Rightarrow Leftrightarrow "
        "longleftarrow longrightarrow longleftrightarrow Longleftarrow Longrightarrow Longleftrightarrow "
        "mapsto longmapsto hookleftarrow hookrightarrow uparrow downarrow updownarrow Uparrow Downarrow "
        "Updownarrow nearrow searrow swarrow nwarrow leftharpoonup leftharpoondown rightharpoonup "
        "rightharpoondown rightleftharpoons implies iff "
        "langle rangle lbrace rbrace lceil rceil lfloor rfloor vert Vert lvert rvert lVert rVert "
        "backslash ldots cdots vdots ddots dots prime dagger ddagger S P degree hline";
    constexpr std::string_view environments =
        "matrix pmatrix bmatrix Bmatrix vmatrix Vmatrix smallmatrix array cases "
        "aligned align gather gathered split equation eqnarray";

    size_t depth = 0;
    size_t operations = 0;
    size_t delimiters = 0;
    std::vector<std::string_view> environmentStack;
    const std::string_view text(normalized);
    for (size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character == '{') {
            if (++depth > kMaximumMathBraceDepth || ++operations > kMaximumMathOperations) return false;
        } else if (character == '}') {
            if (depth == 0) return false;
            --depth;
        } else if (character == '^' || character == '_' || character == '&' ||
            character == '\'' || character == '`' || character == '"' ||
            static_cast<unsigned char>(character) >= 0xC0) {
            // Prime characters and Unicode superscripts can also create nested
            // MicroTeX atoms or expand into commands without an ASCII backslash.
            if (++operations > kMaximumMathOperations) return false;
        } else if (character == '#') {
            // Macro argument placeholders have no role in a display formula.
            return false;
        } else if (character == '\\') {
            if (++operations > kMaximumMathOperations || ++i == text.size()) return false;
            if (!IsAsciiLetter(text[i])) {
                if (std::string_view(" ,:;!{}|_%&#\\").find(text[i]) == std::string_view::npos) return false;
                if (text[i] == '\\') {
                    size_t after = i + 1;
                    while (after < text.size() && (text[after] == ' ' || text[after] == '\n' || text[after] == '\r' || text[after] == '\t')) ++after;
                    // MicroTeX accepts an arbitrary dimension after a row break.
                    if (after < text.size() && text[after] == '[') return false;
                }
                continue;
            }
            const size_t start = i;
            while (i + 1 < text.size() && IsAsciiLetter(text[i + 1])) ++i;
            const auto command = text.substr(start, i - start + 1);
            if (i + 1 < text.size() && text[i + 1] == '@') return false;
            if (command == "begin" || command == "end") {
                size_t position = i + 1;
                while (position < text.size() && (text[position] == ' ' || text[position] == '\n' || text[position] == '\r' || text[position] == '\t')) ++position;
                if (position == text.size() || text[position++] != '{') return false;
                const size_t nameStart = position;
                while (position < text.size() && IsAsciiLetter(text[position])) ++position;
                const auto name = text.substr(nameStart, position - nameStart);
                if (position == text.size() || text[position] != '}' || !IsInMathList(environments, name)) return false;
                if (command == "begin") {
                    if (environmentStack.size() >= kMaximumMathEnvironments) return false;
                    environmentStack.push_back(name);
                    if (name == "array") {
                        ++position;
                        while (position < text.size() && (text[position] == ' ' || text[position] == '\n' || text[position] == '\r' || text[position] == '\t')) ++position;
                        if (position == text.size() || text[position++] != '{') return false;
                        size_t columns = 0;
                        for (; position < text.size() && text[position] != '}'; ++position) {
                            if (text[position] == 'l' || text[position] == 'c' || text[position] == 'r') {
                                if (++columns > 32) return false;
                            } else if (text[position] != '|' && text[position] != ' ') {
                                // In particular, reject *{count}{...} expansion.
                                return false;
                            }
                        }
                        if (columns == 0 || position == text.size()) return false;
                    }
                } else {
                    if (environmentStack.empty() || environmentStack.back() != name) return false;
                    environmentStack.pop_back();
                }
                i = position;
            } else {
                if (!IsInMathList(commands, command)) return false;
                if (command == "left" && ++delimiters > kMaximumMathBraceDepth) return false;
                if (command == "right") {
                    if (delimiters == 0) return false;
                    --delimiters;
                }
            }
        }
    }
    return depth == 0 && delimiters == 0 && environmentStack.empty();
}

bool EnsureInitialized() {
    if (g_initialized) return true;
    if (g_initializationFailed) return false;

    std::filesystem::path root = g_resourceRoot;
    if (root.empty()) {
        root = std::filesystem::current_path() / "res";
    }
    if (!std::filesystem::exists(root / ".clatexmath-res_root")) {
        g_initializationFailed = true;
        return false;
    }

    try {
        tex::LaTeX::init(root.string());
        g_initialized = true;
        return true;
    } catch (...) {
        g_initializationFailed = true;
        return false;
    }
}

} // namespace

void SetMathResourceRoot(const std::filesystem::path& path) {
    std::lock_guard lock(g_mathMutex);
    if (!g_initialized) {
        g_resourceRoot = path;
        g_initializationFailed = false;
    }
}

MathLayout LayoutMath(
    std::string_view source,
    bool displayStyle,
    float fontSize,
    float maximumWidth) {
    MathLayout layout;
    std::string normalized;
    if (!PrepareSafeFormula(source, normalized) || !std::isfinite(fontSize) || fontSize <= 0.0f || fontSize > 1024.0f ||
        !std::isfinite(maximumWidth) || maximumWidth <= 0.0f) {
        return layout;
    }

    std::lock_guard lock(g_mathMutex);
    if (!EnsureInitialized()) return layout;

    try {
        tex::Formula formula(tex::utf82wide(normalized));
        tex::TeXRenderBuilder builder;
        std::unique_ptr<tex::TeXRender> render = std::unique_ptr<tex::TeXRender>(
            builder
                .setStyle(displayStyle ? tex::TexStyle::display : tex::TexStyle::text)
                .setTextSize(fontSize * (displayStyle ? 1.08f : 1.0f))
                .setForeground(tex::black)
                .build(formula));
        if (!render) return layout;

        const float naturalWidth = static_cast<float>(render->getWidth());
        if (naturalWidth > maximumWidth && naturalWidth > 0.0f) {
            render->setTextSize(render->getTextSize() * (maximumWidth / naturalWidth));
        }

        layout.width = std::max(static_cast<float>(render->getWidth()), 1.0f);
        layout.height = std::max(static_cast<float>(render->getHeight()), 1.0f);
        layout.baseline = std::clamp(render->getBaseline(), 0.0f, 1.0f) * layout.height;
        layout.valid = std::isfinite(layout.width) && std::isfinite(layout.height) &&
            std::isfinite(layout.baseline) && layout.height <= 65536.0f &&
            layout.width <= maximumWidth + 1.0f;
        if (layout.valid) {
            layout.renderData = std::make_shared<MathRenderData>();
            layout.renderData->render = std::move(render);
        }
    } catch (...) {
        return {};
    }
    return layout;
}

void DrawMath(
    SkCanvas* canvas,
    const MathLayout& layout,
    float x,
    float y,
    SkColor color) {
    if (!canvas || !layout.valid || !layout.renderData || !layout.renderData->render) return;

    std::lock_guard lock(g_mathMutex);
    try {
        canvas->save();
        layout.renderData->render->setForeground(color);
        auto graphics = CreateMicroTeXSkiaGraphics(canvas);
        layout.renderData->render->draw(
            *graphics,
            static_cast<int>(std::round(x)),
            static_cast<int>(std::round(y)));
        canvas->restore();
    } catch (...) {
        canvas->restore();
    }
}

} // namespace mdviewer
