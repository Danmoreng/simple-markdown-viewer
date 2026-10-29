#pragma once

#include <memory>
#include <string>
#include <string_view>

class SkCanvas;
class SkFontMgr;

namespace mdviewer {

struct MermaidRenderData;

struct MermaidLayout {
    float width = 0.0f;
    float height = 0.0f;
    std::shared_ptr<MermaidRenderData> renderData;
    std::string error;

    [[nodiscard]] bool IsValid() const { return renderData != nullptr; }
};

// Cached native SVG geometry is shared by screen, print and PDF layouts. Width
// and reader zoom affect only the drawing transform, never Mermaid parsing.
MermaidLayout LayoutMermaid(
    std::string_view source, std::string_view fontFamily,
    SkFontMgr* fontManager, float baseFontSize, float maximumWidth);
void DrawMermaid(SkCanvas* canvas, const MermaidLayout& layout, float x, float y, bool dark);

} // namespace mdviewer
