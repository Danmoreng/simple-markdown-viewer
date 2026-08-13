#include "text/text_geometry.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace mdviewer {

namespace {

bool IsAtomicRun(const RunLayout& run) {
    return run.kind == InlineKind::Image || run.kind == InlineKind::Math;
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

    const CaretStop* closest = &run.caretStops.front();
    float closestDistance = std::abs(xInLine - closest->x);
    for (const CaretStop& stop : run.caretStops) {
        const float distance = std::abs(xInLine - stop.x);
        if (distance < closestDistance ||
            (distance == closestDistance && stop.textPosition < closest->textPosition)) {
            closest = &stop;
            closestDistance = distance;
        }
    }
    return closest->textPosition;
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
        if (run.kind == InlineKind::Image) {
            continue;
        }

        const size_t runStart = run.textStart;
        const size_t runEnd = RunLogicalEnd(run);
        const size_t selectedStart = std::max(logicalStart, runStart);
        const size_t selectedEnd = std::min(logicalEnd, runEnd);
        if (selectedStart >= selectedEnd) {
            continue;
        }

        float left = line.x + run.visualX;
        float right = left + run.visualWidth;
        if (!IsAtomicRun(run) && !run.caretStops.empty()) {
            const std::optional<float> startX =
                CaretXForLogicalBoundary(run, selectedStart, false);
            const std::optional<float> endX =
                CaretXForLogicalBoundary(run, selectedEnd, true);
            if (startX.has_value() && endX.has_value()) {
                left = line.x + std::min(*startX, *endX);
                right = line.x + std::max(*startX, *endX);
            }
        }
        rects.push_back(PaddedRect(left, right, top, bottom, horizontalPadding));
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
