#include "markdown/markdown_parser.h"
#include "render/math_renderer.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mdviewer::tests {
namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void MathDisplayLanguageIsBoundedAndStateless() {
    for (const auto source : {
             R"(\frac{-b+\sqrt{b^2-4ac}}{2a})",
             R"(\sum_{n=1}^{\infty}\frac{1}{n^2}=\frac{\pi^2}{6})",
             R"(\begin{pmatrix}a&b\\c&d\end{pmatrix})",
             R"(\begin{array}{r|cl}a&b&c\\d&e&f\end{array})",
             R"(\left\lVert\vec{v}\right\rVert)",
             R"(\operatorname{rank}(A)+\dfrac{1}{2})",
         }) {
        const auto first = LayoutMath(source, true, 17.0f, 600.0f);
        const auto second = LayoutMath(source, true, 17.0f, 600.0f);
        Require(first.valid && second.valid, "ordinary math must render consistently across layouts");
        Require(first.width == second.width && first.height == second.height,
            "repeated math layout must preserve geometry");
    }

    // Include direct definitions, built-in definition aliases, mutable renderer
    // configuration, numeric expansion and internal-name/escaping bypasses.
    for (const auto source : {
             R"(\newcommand{\zz}{\zz}\zz)",
             R"(\renewcommand{\operatorname}{\operatorname}\operatorname{x})",
             R"(\DeclareMathOperator{\zz}{x}\zz)",
             R"(\newenvironment{zz}{\begin{zz}}{}\begin{zz}\end{zz})",
             R"(\newcolumntype{x}{xxxxxxxx}\begin{array}{x}a\end{array})",
             R"(\definecolor{red}{rgb}{0,0,1}x)",
             R"(\magnification{1000000000}x)",
             R"(\DeclareMathSizes{1}{1000000000}{1}{1}x)",
             R"(\fatalIfCmdConflict{false}x)",
             R"(\makeatletter\matrix@@env{x})",
             R"(\Roman{2147483647})",
             R"(\longdiv{1}{0})",
             R"(\rule{1pt}{1000000000pt})",
             R"(\resizebox{1000000000pt}{1000000000pt}{x})",
             R"(\begin{array}{*{1000000000}{c}}x\end{array})",
             R"(\begin{matrix}x\\[1000000000pt]y\end{matrix})",
             R"(\begin{unknown}x\end{unknown})",
             R"(\begin{matrix}x\end{array})",
             R"(\operatorname{#1})",
             R"(\\\newcommand{\zz}{x}\zz)",
             "\\new% split control word\ncommand{\\zz}{x}\\zz",
         }) {
        Require(!LayoutMath(source, true, 17.0f, 600.0f).valid,
            "unsafe or stateful math must remain source instead of entering MicroTeX");
    }
    Require(LayoutMath("x+1 % \\newcommand{\\zz}{\\zz}\n", true, 17.0f, 600.0f).valid,
        "math comments must be stripped consistently before validation and rendering");
    Require(LayoutMath(R"(\text{50\%})", true, 17.0f, 600.0f).valid,
        "an escaped percent is text, not a comment");
    const std::string overlongBackslash = std::string("\xC1\x9C") + "newcommand{" +
        "\xC1\x9C" + "zz}{" + "\xC1\x9C" + "zz}" + "\xC1\x9C" + "zz";
    Require(!LayoutMath(overlongBackslash, true, 17.0f, 600.0f).valid,
        "invalid UTF-8 must not synthesize control sequences in MicroTeX's decoder");

    const std::string tooDeep = std::string(33, '{') + "x" + std::string(33, '}');
    Require(!LayoutMath(tooDeep, true, 17.0f, 600.0f).valid,
        "math nesting must be bounded before native parsing");
    std::string tooManyCommands;
    for (size_t i = 0; i < 257; ++i) tooManyCommands += "\\sqrt ";
    Require(!LayoutMath(tooManyCommands + "x", true, 17.0f, 600.0f).valid,
        "unbraced macro nesting must also have a fixed operation limit");
    Require(!LayoutMath("x" + std::string(257, '\''), true, 17.0f, 600.0f).valid,
        "prime characters must not bypass the nested-atom operation limit");
    std::string superscripts = "x";
    for (size_t i = 0; i < 257; ++i) superscripts += "\xC2\xB2";
    Require(!LayoutMath(superscripts, true, 17.0f, 600.0f).valid,
        "Unicode superscript expansion must also be bounded");
    Require(LayoutMath(R"(\operatorname{rank}(A))", true, 17.0f, 600.0f).valid,
        "rejected definitions must not poison subsequent layouts");
}

void DetailsAreLinearAndDepthBounded() {
    constexpr size_t count = 2000;
    std::string nested;
    for (size_t i = 0; i < count; ++i) nested += "<details><summary>Section</summary>\n\n";
    nested += "Body survives\n\n";
    for (size_t i = 0; i < count; ++i) nested += "</details>\n\n";
    const auto document = MarkdownParser::Parse(nested);
    Require(!document.blocks.empty(), "deep details must retain readable source");
    std::vector<std::pair<const Block*, size_t>> pending;
    for (const auto& block : document.blocks) pending.emplace_back(&block, 0);
    size_t maximumDetailsDepth = 0;
    size_t rawBlocks = 0;
    bool sawBody = false;
    while (!pending.empty()) {
        const auto [block, parentDepth] = pending.back();
        pending.pop_back();
        const size_t depth = parentDepth + (block->type == BlockType::Details ? 1U : 0U);
        maximumDetailsDepth = std::max(maximumDetailsDepth, depth);
        if (block->type == BlockType::RawHtml) ++rawBlocks;
        for (const auto& run : block->inlineRuns) {
            if (run.text.find("Body survives") != std::string::npos) sawBody = true;
        }
        for (const auto& child : block->children) pending.emplace_back(&child, depth);
    }
    Require(maximumDetailsDepth == 32, "HTML details nesting must stop at 32 levels");
    Require(rawBlocks > 0 && sawBody, "over-limit details and their content must remain visible");

    std::string unmatched;
    for (size_t i = 0; i < count; ++i) unmatched += "<details><summary>Unclosed</summary>\n\n";
    const auto unmatchedDocument = MarkdownParser::Parse(unmatched);
    Require(unmatchedDocument.blocks.size() == count,
        "unmatched details must be processed once and preserved individually");
    Require(unmatchedDocument.blocks.front().type == BlockType::RawHtml,
        "an unmatched opening must remain source");

    const auto ordinary = MarkdownParser::Parse(
        "<details open><summary><kbd>Outer</kbd></summary>\n\n"
        "Before\n\n<details><summary>Inner</summary>\n\nInside\n\n</details>\n\n"
        "After\n\n</details>\n\nTail\n");
    Require(ordinary.blocks.size() == 2 && ordinary.blocks[0].type == BlockType::Details,
        "ordinary details and following siblings must retain structure");
    const auto& outer = ordinary.blocks[0];
    Require(outer.detailsOpen && outer.children.size() == 3 &&
        outer.children[1].type == BlockType::Details &&
        outer.detailsId != outer.children[1].detailsId,
        "details state, nested content and unique identifiers must survive conversion");
}

void ImageRunsExistWithoutAltText() {
    const auto document = MarkdownParser::Parse("![](one.png)![](two.png)\n");
    Require(document.blocks.size() == 1 && document.blocks[0].inlineRuns.size() == 2,
        "adjacent empty-alt images must each have a distinct run");
    const auto& runs = document.blocks[0].inlineRuns;
    Require(runs[0].kind == InlineKind::Image && runs[0].text.empty() && runs[0].imageSource == "one.png" &&
        runs[1].kind == InlineKind::Image && runs[1].imageSource == "two.png",
        "empty-alt images must retain their sources");
    const auto linked = MarkdownParser::Parse("[![](picture.png)](target.md)\n");
    Require(linked.blocks[0].inlineRuns[0].kind == InlineKind::Image &&
        linked.blocks[0].inlineRuns[0].linkTarget == "target.md",
        "linked images without alt text must retain the enclosing link");
    const auto formatted = MarkdownParser::Parse("![**bold** &amp; plain](picture.png)\n");
    Require(formatted.blocks[0].inlineRuns.size() == 1 &&
        formatted.blocks[0].inlineRuns[0].text == "bold & plain",
        "formatted alt text must produce one image with decoded plain alt text");
}

} // namespace

void RunParserSafetyTests() {
    MathDisplayLanguageIsBoundedAndStateless();
    DetailsAreLinearAndDepthBounded();
    ImageRunsExistWithoutAltText();
}

} // namespace mdviewer::tests
