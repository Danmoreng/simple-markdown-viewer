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
    std::string linkTarget;
    std::string imageSource;
    std::string tableTsv;
    std::string tableCsv;
    size_t detailsToggleId = 0;
};

struct HitTestCallbacks {
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
