#include "render/math_renderer.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>

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

namespace mdviewer {

struct MathRenderData {
    std::unique_ptr<tex::TeXRender> render;
};

namespace {

constexpr size_t kMaximumMathSourceBytes = 8192;
constexpr size_t kMaximumMathBraceDepth = 128;
std::mutex g_mathMutex;
std::filesystem::path g_resourceRoot;
bool g_initialized = false;
bool g_initializationFailed = false;

bool IsSafeFormulaSize(std::string_view source) {
    if (source.empty() || source.size() > kMaximumMathSourceBytes ||
        source.find('\0') != std::string_view::npos) {
        return false;
    }

    size_t depth = 0;
    bool escaped = false;
    for (const char character : source) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == '\\') {
            escaped = true;
        } else if (character == '{') {
            if (++depth > kMaximumMathBraceDepth) return false;
        } else if (character == '}' && depth > 0) {
            --depth;
        }
    }
    return true;
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
    if (!IsSafeFormulaSize(source) || !std::isfinite(fontSize) || fontSize <= 0.0f ||
        !std::isfinite(maximumWidth) || maximumWidth <= 0.0f) {
        return layout;
    }

    std::lock_guard lock(g_mathMutex);
    if (!EnsureInitialized()) return layout;

    try {
        tex::Formula formula(tex::utf82wide(std::string(source)));
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
