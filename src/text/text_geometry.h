#pragma once

#include <cstddef>
#include <vector>

#include "layout/layout_engine.h"

namespace mdviewer {

size_t HitTestTextRun(const RunLayout& run, float xInLine);

std::vector<SkRect> GetTextRangeRects(
    const LineLayout& line,
    size_t logicalStart,
    size_t logicalEnd,
    float top,
    float bottom,
    float horizontalPadding = 0.0f);

} // namespace mdviewer
