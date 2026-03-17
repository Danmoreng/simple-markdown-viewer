#pragma once
#include <vector>
#include "layout/document_model.h"
#include "include/core/SkRect.h"

class SkTypeface;

namespace mdviewer {

struct RunLayout {
    InlineStyle style;
    std::string text;
    size_t textStart = 0;
};

struct LineLayout {
    float y;
    float height;
    size_t textStart = 0;
    size_t textLength = 0;
    std::vector<RunLayout> runs;
};

struct BlockLayout {
    BlockType type;
    SkRect bounds;
    size_t textStart = 0;
    size_t textLength = 0;
    std::vector<LineLayout> lines;
    std::vector<BlockLayout> children;
};

struct DocumentLayout {
    std::vector<BlockLayout> blocks;
    float totalHeight;
    std::string plainText;
};

class LayoutEngine {
public:
    static DocumentLayout ComputeLayout(const DocumentModel& doc, float width, SkTypeface* typeface);
};

} // namespace mdviewer
