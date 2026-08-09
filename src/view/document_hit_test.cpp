#include "view/document_hit_test.h"

#include <limits>

namespace mdviewer {
namespace {

struct ClosestLine {
    const BlockLayout* block = nullptr;
    const LineLayout* line = nullptr;
    float distance = std::numeric_limits<float>::max();
};

size_t GetRunTextEnd(const RunLayout& run) {
    if (run.kind == InlineKind::Image) {
        return run.textStart;
    }
    return run.textStart + run.text.size();
}

float GetVerticalDistance(const LineLayout& line, float documentY) {
    if (documentY < line.y) {
        return line.y - documentY;
    }
    const float lineBottom = line.y + line.height;
    if (documentY > lineBottom) {
        return documentY - lineBottom;
    }
    return 0.0f;
}

void FindClosestLine(
    const std::vector<BlockLayout>& blocks,
    float documentY,
    ClosestLine& closest) {
    for (const auto& block : blocks) {
        for (const auto& line : block.lines) {
            const float distance = GetVerticalDistance(line, documentY);
            if (distance <= closest.distance) {
                closest.block = &block;
                closest.line = &line;
                closest.distance = distance;
            }
        }
        FindClosestLine(block.children, documentY, closest);
    }
}

DocumentTextHit HitTestLine(
    const BlockLayout& block,
    const LineLayout& line,
    float x,
    const HitTestCallbacks& callbacks) {
    DocumentTextHit hit;
    if (callbacks.get_block_horizontal_scroll) {
        x += callbacks.get_block_horizontal_scroll(block);
    }
    if (line.runs.empty()) {
        hit.position = line.textStart;
        hit.valid = true;
        return hit;
    }

    float currentX = line.x;
    size_t fallbackPosition = line.textStart;
    for (size_t runIndex = 0; runIndex < line.runs.size(); ++runIndex) {
        const auto& run = line.runs[runIndex];
        const float runWidth = callbacks.get_run_visual_width(block, line, run);
        const float runEndX = currentX + runWidth;
        fallbackPosition = GetRunTextEnd(run);

        if (x <= runEndX || runIndex + 1 == line.runs.size()) {
            hit.position = callbacks.find_text_position_in_run(block, line, run, x - currentX);
            hit.valid = true;
            hit.url = run.linkTarget.empty() ? run.imageSource : run.linkTarget;
            hit.formatting = run.formatting;
            hit.kind = run.kind;
            return hit;
        }
        currentX = runEndX;
    }

    hit.position = fallbackPosition;
    hit.valid = true;
    return hit;
}

} // namespace

DocumentTextHit HitTestDocument(
    const DocumentLayout& layout,
    float scrollOffset,
    float contentTopInset,
    float x,
    float viewportY,
    const HitTestCallbacks& callbacks) {
    if (viewportY < contentTopInset) {
        return {};
    }

    const float documentY = (viewportY - contentTopInset) + scrollOffset;
    ClosestLine closest;
    FindClosestLine(layout.blocks, documentY, closest);
    if (closest.block == nullptr || closest.line == nullptr) {
        return {};
    }

    return HitTestLine(*closest.block, *closest.line, x, callbacks);
}

} // namespace mdviewer
