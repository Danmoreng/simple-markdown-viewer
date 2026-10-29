#include "text/text_geometry.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace mdviewer {

namespace {

bool IsAtomicRun(const RunLayout& run) {
    return run.kind == InlineKind::Image || run.kind == InlineKind::Math ||
        run.kind == InlineKind::Diagram;
}

size_t RunLogicalEnd(const RunLayout& run) {
    return run.textStart + run.text.size();
}

size_t AtomicHitPosition(const RunLayout& run, float xInLine) {
    if (run.kind == InlineKind::Image || run.text.empty()) {
        return run.textStart;
    }

    const bool rightHalf = xInLine >= run.visualX + (run.visualWidth * 0.5f);
    const bool isRtl = (run.bidiLevel & 1U) != 0;
    if (rightHalf == isRtl) {
        return run.textStart;
    }
    return RunLogicalEnd(run);
}

std::optional<float> CaretXForLogicalBoundary(
    const RunLayout& run,
    size_t logicalPosition,
    bool snapForward) {
    if (run.caretStopsLogicalOrder != 0 && !run.caretStops.empty()) {
        const auto findBoundary = [&](auto begin, auto end) -> std::optional<float> {
            const auto next = std::lower_bound(begin, end, logicalPosition,
                [](const CaretStop& stop, size_t position) { return stop.textPosition < position; });
            auto selected = next;
            if (next == end || next->textPosition != logicalPosition) {
                if ((!snapForward || next == end) && next != begin) selected = std::prev(next);
            }
            if (selected == end) return std::nullopt;
            // Preserve the existing visual-first tie policy for duplicate logical
            // boundaries (zero-advance glyphs and cluster-edge carets).
            const size_t position = selected->textPosition;
            auto first = std::lower_bound(begin, end, position,
                [](const CaretStop& stop, size_t value) { return stop.textPosition < value; });
            if (run.caretStopsLogicalOrder < 0) {
                first = std::prev(std::upper_bound(first, end, position,
                    [](size_t value, const CaretStop& stop) { return value < stop.textPosition; }));
            }
            return first->x;
        };
        return run.caretStopsLogicalOrder > 0
            ? findBoundary(run.caretStops.begin(), run.caretStops.end())
            : findBoundary(run.caretStops.rbegin(), run.caretStops.rend());
    }
    const CaretStop* exact = nullptr;
    const CaretStop* preceding = nullptr;
    const CaretStop* following = nullptr;
    for (const CaretStop& stop : run.caretStops) {
        if (stop.textPosition == logicalPosition) {
            exact = &stop;
            break;
        }
        if (stop.textPosition < logicalPosition &&
            (preceding == nullptr || stop.textPosition > preceding->textPosition)) {
            preceding = &stop;
        }
        if (stop.textPosition > logicalPosition &&
            (following == nullptr || stop.textPosition < following->textPosition)) {
            following = &stop;
        }
    }
    if (exact != nullptr) {
        return exact->x;
    }
    const CaretStop* snapped = snapForward
        ? (following != nullptr ? following : preceding)
        : (preceding != nullptr ? preceding : following);
    if (snapped != nullptr) {
        return snapped->x;
    }
    return std::nullopt;
}

SkRect PaddedRect(float left, float right, float top, float bottom, float padding) {
    padding = std::max(padding, 0.0f);
    const float rectLeft = std::min(left, right);
    const float rectRight = std::max(left, right);
    return SkRect::MakeLTRB(rectLeft - padding, top, rectRight + padding, bottom);
}

} // namespace

size_t HitTestTextRun(const RunLayout& run, float xInLine) {
    if (IsAtomicRun(run) || run.caretStops.empty()) {
        return AtomicHitPosition(run, xInLine);
    }

    // Hand-constructed/legacy runs may retain logical rather than visual order.
    // Only layout-indexed runs carry the ordering metadata needed by the fast path.
    if (run.caretStopsLogicalOrder == 0) {
        const CaretStop* closest = &run.caretStops.front();
        for (const CaretStop& stop : run.caretStops) {
            const float distance = std::abs(xInLine - stop.x);
            const float closestDistance = std::abs(xInLine - closest->x);
            if (distance < closestDistance ||
                (distance == closestDistance && stop.textPosition < closest->textPosition)) {
                closest = &stop;
            }
        }
        return closest->textPosition;
    }

    // Layout stores carets in visual order, with equal-X stops sorted logically.
    const auto next = std::lower_bound(run.caretStops.begin(), run.caretStops.end(), xInLine,
        [](const CaretStop& stop, float x) { return stop.x < x; });
    const CaretStop* closest = next != run.caretStops.end() ? &*next : nullptr;
    if (next != run.caretStops.begin()) {
        const float previousX = std::prev(next)->x;
        const auto previous = std::lower_bound(run.caretStops.begin(), next, previousX,
            [](const CaretStop& stop, float x) { return stop.x < x; });
        if (closest == nullptr || std::abs(xInLine - previousX) < std::abs(xInLine - closest->x) ||
            (std::abs(xInLine - previousX) == std::abs(xInLine - closest->x) &&
             previous->textPosition < closest->textPosition)) {
            closest = &*previous;
        }
    }
    return closest->textPosition;
}

std::optional<SkRect> GetTextRunRangeRect(
    const RunLayout& run, float lineX, size_t logicalStart, size_t logicalEnd,
    float top, float bottom, float horizontalPadding) {
    if (run.kind == InlineKind::Image || bottom <= top) return std::nullopt;
    if (logicalEnd < logicalStart) std::swap(logicalStart, logicalEnd);
    const size_t selectedStart = std::max(logicalStart, run.textStart);
    const size_t selectedEnd = std::min(logicalEnd, RunLogicalEnd(run));
    if (selectedStart >= selectedEnd) return std::nullopt;
    float left = lineX + run.visualX;
    float right = left + run.visualWidth;
    if (!IsAtomicRun(run) && !run.caretStops.empty()) {
        const auto startX = CaretXForLogicalBoundary(run, selectedStart, false);
        const auto endX = CaretXForLogicalBoundary(run, selectedEnd, true);
        if (startX && endX) {
            left = lineX + std::min(*startX, *endX);
            right = lineX + std::max(*startX, *endX);
        }
    }
    return PaddedRect(left, right, top, bottom, horizontalPadding);
}

std::vector<SkRect> GetTextRangeRects(
    const LineLayout& line,
    size_t logicalStart,
    size_t logicalEnd,
    float top,
    float bottom,
    float horizontalPadding) {
    if (logicalEnd < logicalStart) {
        std::swap(logicalStart, logicalEnd);
    }
    if (logicalStart == logicalEnd || bottom <= top) {
        return {};
    }

    std::vector<SkRect> rects;
    for (const RunLayout& run : line.runs) {
        if (const auto rect = GetTextRunRangeRect(run, line.x, logicalStart, logicalEnd, top, bottom, horizontalPadding)) {
            rects.push_back(*rect);
        }
    }

    std::sort(rects.begin(), rects.end(), [](const SkRect& left, const SkRect& right) {
        return left.left() < right.left();
    });
    std::vector<SkRect> merged;
    constexpr float kMergeTolerance = 0.01f;
    for (const SkRect& rect : rects) {
        if (!merged.empty() && rect.left() <= merged.back().right() + kMergeTolerance) {
            merged.back().setLTRB(
                merged.back().left(),
                std::min(merged.back().top(), rect.top()),
                std::max(merged.back().right(), rect.right()),
                std::max(merged.back().bottom(), rect.bottom()));
        } else {
            merged.push_back(rect);
        }
    }
    return merged;
}

} // namespace mdviewer
