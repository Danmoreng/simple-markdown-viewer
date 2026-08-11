#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "include/core/SkColor.h"

class SkCanvas;

namespace mdviewer {

struct MathRenderData;

struct MathLayout {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    bool valid = false;
    std::shared_ptr<MathRenderData> renderData;
};

void SetMathResourceRoot(const std::filesystem::path& path);
MathLayout LayoutMath(
    std::string_view source,
    bool displayStyle,
    float fontSize,
    float maximumWidth);
void DrawMath(
    SkCanvas* canvas,
    const MathLayout& layout,
    float x,
    float y,
    SkColor color);

} // namespace mdviewer
