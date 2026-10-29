#include "view/document_hit_test.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "text/text_geometry.h"

namespace mdviewer {
namespace {

struct ClosestLine {
    const BlockLayout* block = nullptr;
    const LineLayout* line = nullptr;
    float distance = std::numeric_limits<float>::max();
    float horizontalDistance = std::numeric_limits<float>::max();
    bool insideHorizontalClip = true;
};

void ClearSemanticHit(DocumentTextHit& hit) {
    hit.url.clear();
    hit.formatting = InlineFormatting::None;
    hit.linkTarget.clear();
    hit.imageSource.clear();
    hit.kind = InlineKind::Text;
    hit.diagram = {};
    hit.diagramTextStart = 0;
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
    float x,
    float documentY,
    const HitTestCallbacks& callbacks,
    ClosestLine& closest,
    float clipLeft = -std::numeric_limits<float>::infinity(),
    float clipRight = std::numeric_limits<float>::infinity()) {
    const auto visitBlock = [&](const BlockLayout& block) {
        float childClipLeft = clipLeft;
        float childClipRight = clipRight;
        if (block.horizontalContentWidth > block.horizontalViewportWidth + 0.5f) {
            const float inset = block.type == BlockType::CodeBlock ? 8.0f : 0.0f;
            childClipLeft = std::max(childClipLeft, block.bounds.left() + inset);
            childClipRight = std::min(childClipRight, block.bounds.left() + inset + block.horizontalViewportWidth);
        }
        const float localX = x + (callbacks.get_block_horizontal_scroll
            ? callbacks.get_block_horizontal_scroll(block) : 0.0f);
        const auto consider = [&](const LineLayout& line) {
            const float distance = GetVerticalDistance(line, documentY);
            const bool tableCell = block.type == BlockType::TableCell ||
                block.type == BlockType::TableHeaderCell;
            const float left = tableCell ? block.bounds.left() : line.x;
            const float right = tableCell ? block.bounds.right() : line.x + line.width;
            const float horizontalDistance = localX < left ? left - localX
                : localX > right ? localX - right : 0.0f;
            if (distance < closest.distance ||
                (distance == closest.distance && horizontalDistance < closest.horizontalDistance)) {
                closest.block = &block;
                closest.line = &line;
                closest.distance = distance;
                closest.horizontalDistance = horizontalDistance;
                closest.insideHorizontalClip = x >= childClipLeft && x <= childClipRight;
            }
        };
        // Lines are laid out monotonically, including wrapped paragraphs and code.
        const auto next = std::lower_bound(block.lines.begin(), block.lines.end(), documentY,
            [](const LineLayout& line, float y) { return line.y + line.height <= y; });
        if (next != block.lines.end()) consider(*next);
        if (next != block.lines.begin()) consider(*std::prev(next));
        FindClosestLine(block.children, x, documentY, callbacks, closest, childClipLeft, childClipRight);
    };

    // Walk outwards from the insertion point. Once a sibling's bounding box is
    // farther away than our best line, every later sibling on that side is too.
    const auto split = std::upper_bound(blocks.begin(), blocks.end(), documentY,
        [](float y, const BlockLayout& block) { return y < block.bounds.top(); });
    for (auto it = split; it != blocks.end(); ++it) {
        if (!it->bounds.isEmpty() && it->bounds.top() - documentY > closest.distance) break;
        visitBlock(*it);
    }
    for (auto it = std::make_reverse_iterator(split); it != blocks.rend(); ++it) {
        if (!it->bounds.isEmpty() && documentY - it->bounds.bottom() > closest.distance) break;
        visitBlock(*it);
    }
}

template <typename Visitor>
void VisitBlocksAtY(const std::vector<BlockLayout>& blocks, float y, Visitor visitor) {
    auto first = std::lower_bound(blocks.begin(), blocks.end(), y,
        [](const BlockLayout& block, float value) { return block.bounds.bottom() < value; });
    for (; first != blocks.end() && first->bounds.top() <= y; ++first) {
        if (!visitor(*first)) {
            return;
        }
    }
}

const BlockLayout* FindTableAtPoint(
    const std::vector<BlockLayout>& blocks,
    float x,
    float documentY) {
    const BlockLayout* result = nullptr;
    VisitBlocksAtY(blocks, documentY, [&](const BlockLayout& block) {
        if (block.type == BlockType::Table && block.bounds.contains(x, documentY)) {
            result = &block;
            return false;
        }
        if (const BlockLayout* nested = FindTableAtPoint(block.children, x, documentY)) {
            result = nested;
            return false;
        }
        return true;
    });
    return result;
}

size_t FindDetailsToggleAtPoint(
    const std::vector<BlockLayout>& blocks,
    float x,
    float documentY) {
    size_t result = 0;
    VisitBlocksAtY(blocks, documentY, [&](const BlockLayout& block) {
        if (block.type == BlockType::Details && block.detailsId != 0 && !block.lines.empty()) {
            const float summaryBottom = block.lines.back().y + block.lines.back().height + 5.0f;
            if (x >= block.bounds.left() && x <= block.bounds.right() &&
                documentY >= block.bounds.top() && documentY <= summaryBottom) {
                result = block.detailsId;
                return false;
            }
        }
        if (const size_t nested = FindDetailsToggleAtPoint(block.children, x, documentY); nested != 0) {
            result = nested;
            return false;
        }
        return true;
    });
    return result;
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
            if (closestRun->imageLoadRequired || !closestRun->imageErrorLines.empty()) {
                // Only the separately registered Load image button grants consent.
                hit.url.clear();
                hit.linkTarget.clear();
            }
        }
        if (closestRun->kind == InlineKind::Diagram) {
            hit.diagram = closestRun->mermaidLayout;
            hit.diagramTextStart = closestRun->textStart;
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
    const BlockLayout* table = callbacks.includeTableData
        ? FindTableAtPoint(layout.blocks, x, documentY) : nullptr;
    const size_t detailsToggleId = FindDetailsToggleAtPoint(layout.blocks, x, documentY);
    ClosestLine closest;
    FindClosestLine(layout.blocks, x, documentY, callbacks, closest);
    if (closest.block == nullptr || closest.line == nullptr) {
        return {};
    }

    DocumentTextHit hit = HitTestLine(*closest.block, *closest.line, x, callbacks);
    if (closest.distance > 0.0f || !closest.insideHorizontalClip) {
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
