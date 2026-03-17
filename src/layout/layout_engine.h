#pragma once
#include <vector>
#include "layout/document_model.h"
#include "include/core/SkRect.h"

namespace mdviewer {

struct LineLayout {
    float y;
    float height;
    std::vector<InlineRun> runs; // Simplified for now: one run per style change
};

struct BlockLayout {
    BlockType type;
    SkRect bounds;
    std::vector<LineLayout> lines;
    std::vector<BlockLayout> children;
};

struct DocumentLayout {
    std::vector<BlockLayout> blocks;
    float totalHeight;
};

class LayoutEngine {
public:
    static DocumentLayout ComputeLayout(const DocumentModel& doc, float width);
};

} // namespace mdviewer
