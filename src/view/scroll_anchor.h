#pragma once

#include <cstddef>

namespace mdviewer {

struct ScrollAnchor {
    size_t textPosition = 0;
    float lineOffsetRatio = 0.0f;
    float scrollRatio = 0.0f;
    bool valid = false;
};

} // namespace mdviewer
