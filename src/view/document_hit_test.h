#pragma once

#include <functional>
#include <string>

#include "layout/layout_engine.h"

namespace mdviewer {

struct DocumentTextHit {
    size_t position = 0;
    bool valid = false;
    std::string url;
    InlineFormatting formatting = InlineFormatting::None;
    InlineKind kind = InlineKind::Text;
};

struct HitTestCallbacks {
    std::function<float(const BlockLayout& block, const LineLayout& line, const RunLayout& run)> get_run_visual_width;
    std::function<size_t(const BlockLayout& block, const LineLayout& line, const RunLayout& run, float x_in_run)> find_text_position_in_run;
    std::function<float(const BlockLayout& block)> get_block_horizontal_scroll;
};

DocumentTextHit HitTestDocument(
    const DocumentLayout& layout,
    float scrollOffset,
    float contentTopInset,
    float x,
    float viewportY,
    const HitTestCallbacks& callbacks);

} // namespace mdviewer
