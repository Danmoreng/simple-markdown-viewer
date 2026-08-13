#include "view/document_hit_test.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "text/text_geometry.h"

namespace mdviewer {
namespace {

struct ClosestLine {
    const BlockLayout* block = nullptr;
    const LineLayout* line = nullptr;
    float distance = std::numeric_limits<float>::max();
};

void ClearSemanticHit(DocumentTextHit& hit) {
    hit.url.clear();
    hit.formatting = InlineFormatting::None;
    hit.linkTarget.clear();
    hit.imageSource.clear();
    hit.kind = InlineKind::Text;
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

const BlockLayout* FindTableAtPoint(
    const std::vector<BlockLayout>& blocks,
    float x,
    float documentY) {
    for (const auto& block : blocks) {
        if (block.type == BlockType::Table && block.bounds.contains(x, documentY)) {
            return &block;
        }
        if (const BlockLayout* nested = FindTableAtPoint(block.children, x, documentY)) {
            return nested;
        }
    }
    return nullptr;
}

size_t FindDetailsToggleAtPoint(
    const std::vector<BlockLayout>& blocks,
    float x,
    float documentY) {
    for (const auto& block : blocks) {
        if (block.type == BlockType::Details && block.detailsId != 0 && !block.lines.empty()) {
            const float summaryBottom = block.lines.back().y + block.lines.back().height + 5.0f;
            if (x >= block.bounds.left() && x <= block.bounds.right() &&
                documentY >= block.bounds.top() && documentY <= summaryBottom) {
                return block.detailsId;
            }
        }
        if (const size_t nested = FindDetailsToggleAtPoint(block.children, x, documentY); nested != 0) {
            return nested;
        }
    }
    return 0;
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

    const RunLayout* closestRun = nullptr;
    float closestDistance = std::numeric_limits<float>::max();
    bool insideClosestRun = false;
    for (const RunLayout& run : line.runs) {
        const float runLeft = line.x + run.visualX;
        const float runRight = runLeft + std::max(run.visualWidth, 0.0f);
        const bool inside = x >= runLeft && x <= runRight;
        const float distance = inside
            ? 0.0f
            : std::min(std::abs(x - runLeft), std::abs(x - runRight));
        if (closestRun == nullptr || distance < closestDistance) {
            closestRun = &run;
            closestDistance = distance;
            insideClosestRun = inside;
            if (inside) {
                break;
            }
        }
    }

    if (closestRun == nullptr) {
        hit.position = line.textStart;
        hit.valid = true;
        return hit;
    }

    hit.position = HitTestTextRun(*closestRun, x - line.x);
    hit.valid = true;
    if (insideClosestRun) {
        hit.url = closestRun->linkTarget.empty()
            ? closestRun->imageSource
            : closestRun->linkTarget;
        hit.formatting = closestRun->formatting;
        hit.kind = closestRun->kind;
        hit.linkTarget = closestRun->linkTarget;
        if (closestRun->kind == InlineKind::Image) {
            hit.imageSource = closestRun->imageSource;
        }
    }
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
    const BlockLayout* table = FindTableAtPoint(layout.blocks, x, documentY);
    const size_t detailsToggleId = FindDetailsToggleAtPoint(layout.blocks, x, documentY);
    ClosestLine closest;
    FindClosestLine(layout.blocks, documentY, closest);
    if (closest.block == nullptr || closest.line == nullptr) {
        return {};
    }

    DocumentTextHit hit = HitTestLine(*closest.block, *closest.line, x, callbacks);
    if (closest.distance > 0.0f) {
        ClearSemanticHit(hit);
    }
    if (table != nullptr) {
        hit.tableTsv = table->tableTsv;
        hit.tableCsv = table->tableCsv;
    }
    hit.detailsToggleId = detailsToggleId;
    return hit;
}

} // namespace mdviewer
