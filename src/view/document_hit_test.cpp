#include "view/document_hit_test.h"

#include <limits>

namespace mdviewer {
namespace {

size_t GetRunTextEnd(const RunLayout& run) {
    if (run.style == InlineStyle::Image) {
        return run.textStart;
    }
    return run.textStart + run.text.size();
}

bool FindBestHit(
    const std::vector<BlockLayout>& blocks,
    float x,
    float documentY,
    DocumentTextHit& bestHit,
    float& bestDistance,
    const HitTestCallbacks& callbacks) {
    for (const auto& block : blocks) {
        for (const auto& line : block.lines) {
            const float lineTop = line.y;
            const float lineBottom = line.y + line.height;
            float distance = 0.0f;
            if (documentY < lineTop) {
                distance = lineTop - documentY;
            } else if (documentY > lineBottom) {
                distance = documentY - lineBottom;
            }

            if (distance > bestDistance) {
                continue;
            }

            float currentX = line.x;
            size_t fallbackPosition = line.textStart;
            bool foundRun = false;

            for (const auto& run : line.runs) {
                const float runWidth = callbacks.get_run_visual_width(block, line, run);
                const float runEndX = currentX + runWidth;
                fallbackPosition = GetRunTextEnd(run);

                if (x <= runEndX || &run == &line.runs.back()) {
                    bestHit.position = callbacks.find_text_position_in_run(block, line, run, x - currentX);
                    bestHit.valid = true;
                    bestHit.url = run.url;
                    bestHit.style = run.style;
                    bestDistance = distance;
                    foundRun = true;
                    break;
                }

                currentX = runEndX;
            }

            if (!foundRun && line.runs.empty()) {
                bestHit.position = line.textStart;
                bestHit.valid = true;
                bestDistance = distance;
            } else if (!foundRun && fallbackPosition >= line.textStart) {
                bestHit.position = fallbackPosition;
                bestHit.valid = true;
                bestDistance = distance;
            }
        }

        if (FindBestHit(block.children, x, documentY, bestHit, bestDistance, callbacks)) {
            // bestHit updated by recursion
        }
    }

    return bestHit.valid;
}

} // namespace

DocumentTextHit HitTestDocument(
    const DocumentLayout& layout,
    float scrollOffset,
    float contentTopInset,
    float x,
    float viewportY,
    const HitTestCallbacks& callbacks) {
    DocumentTextHit hit;
    if (viewportY < contentTopInset) {
        return hit;
    }

    const float documentY = (viewportY - contentTopInset) + scrollOffset;
    float bestDistance = std::numeric_limits<float>::max();
    FindBestHit(layout.blocks, x, documentY, hit, bestDistance, callbacks);
    return hit;
}

} // namespace mdviewer
