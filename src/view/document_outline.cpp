#include "view/document_outline.h"

#include <algorithm>
#include <cmath>

#include "view/document_interaction.h"

namespace mdviewer {

float GetOutlineSidebarWidth(const AppState& appState) {
    if (appState.docLayout.outline.empty()) {
        return 0.0f;
    }
    return appState.outlineCollapsed ? kOutlineCollapsedWidth : ClampOutlineWidth(appState.outlineWidth);
}

float GetOutlineX(const AppState& appState, float surfaceWidth) {
    const float width = GetOutlineSidebarWidth(appState);
    if (width <= 0.0f || appState.outlineSide == OutlineSide::Left) {
        return 0.0f;
    }
    return std::max(surfaceWidth - width, 0.0f);
}

float GetOutlineDividerX(const AppState& appState, float surfaceWidth) {
    return appState.outlineSide == OutlineSide::Left
        ? GetOutlineSidebarWidth(appState)
        : GetOutlineX(appState, surfaceWidth);
}

SkRect GetOutlineToggleRect(const AppState& appState, float surfaceWidth, float contentTopInset) {
    return SkRect::MakeXYWH(
        GetOutlineDividerX(appState, surfaceWidth) - (kOutlineToggleSize * 0.5f),
        contentTopInset + kOutlineToggleTopPadding,
        kOutlineToggleSize,
        kOutlineToggleSize);
}

float GetOutlineViewportHeight(float surfaceHeight, float contentTopInset) {
    const float contentTop = contentTopInset + kOutlineTopPadding;
    return std::max(surfaceHeight - contentTop - kOutlineBottomPadding, 0.0f);
}

float GetMaxOutlineScroll(const AppState& appState, float surfaceHeight, float contentTopInset) {
    const float contentHeight = static_cast<float>(appState.docLayout.outline.size()) * kOutlineItemHeight;
    return std::max(contentHeight - GetOutlineViewportHeight(surfaceHeight, contentTopInset), 0.0f);
}

bool IsPointInOutlineSidebar(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset) {
    const float width = GetOutlineSidebarWidth(appState);
    const float outlineX = GetOutlineX(appState, surfaceWidth);
    return width > 0.0f && x >= outlineX && x < outlineX + width && y >= contentTopInset;
}

bool HitTestOutlineResizeHandle(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float surfaceHeight,
    float contentTopInset) {
    if (appState.outlineCollapsed || appState.docLayout.outline.empty() ||
        y < contentTopInset || y >= surfaceHeight) {
        return false;
    }
    if (GetOutlineToggleRect(appState, surfaceWidth, contentTopInset).contains(x, y)) {
        return false;
    }

    const float dividerX = GetOutlineDividerX(appState, surfaceWidth);
    return std::abs(x - dividerX) <= kOutlineResizeHandleWidth * 0.5f;
}

bool ResizeOutlineSidebar(AppState& appState, float pointerX, float surfaceWidth) {
    const float requestedWidth = appState.outlineSide == OutlineSide::Left
        ? pointerX
        : surfaceWidth - pointerX;
    const float clampedWidth = ClampOutlineWidth(requestedWidth);
    if (std::abs(clampedWidth - appState.outlineWidth) < 0.01f) {
        return false;
    }

    appState.outlineWidth = clampedWidth;
    appState.needsRepaint = true;
    return true;
}

void EnsureOutlineIndexVisible(
    AppState& appState,
    size_t index,
    float surfaceHeight,
    float contentTopInset) {
    if (appState.docLayout.outline.empty()) {
        appState.outlineScrollOffset = 0.0f;
        return;
    }

    const float viewportHeight = GetOutlineViewportHeight(surfaceHeight, contentTopInset);
    const float itemTop = static_cast<float>(std::min(index, appState.docLayout.outline.size() - 1)) * kOutlineItemHeight;
    const float itemBottom = itemTop + kOutlineItemHeight;
    if (itemTop < appState.outlineScrollOffset) {
        appState.outlineScrollOffset = itemTop;
    } else if (itemBottom > appState.outlineScrollOffset + viewportHeight) {
        appState.outlineScrollOffset = itemBottom - viewportHeight;
    }
    appState.outlineScrollOffset = std::clamp(
        appState.outlineScrollOffset,
        0.0f,
        GetMaxOutlineScroll(appState, surfaceHeight, contentTopInset));
}

void SyncOutlineScrollToDocument(AppState& appState, float surfaceHeight, float contentTopInset) {
    if (appState.docLayout.outline.empty() || appState.outlineCollapsed) {
        appState.outlineScrollOffset = 0.0f;
        appState.outlineLastDocumentScrollOffset = appState.scrollOffset;
        appState.outlineLastViewportHeight = surfaceHeight;
        return;
    }

    const bool documentMoved = std::abs(appState.scrollOffset - appState.outlineLastDocumentScrollOffset) > 0.01f;
    const bool viewportChanged = std::abs(surfaceHeight - appState.outlineLastViewportHeight) > 0.01f;
    appState.outlineScrollOffset = std::clamp(
        appState.outlineScrollOffset,
        0.0f,
        GetMaxOutlineScroll(appState, surfaceHeight, contentTopInset));
    if (documentMoved || viewportChanged) {
        EnsureOutlineIndexVisible(
            appState,
            GetCurrentOutlineIndex(appState.docLayout, appState.scrollOffset),
            surfaceHeight,
            contentTopInset);
    }
    appState.outlineLastDocumentScrollOffset = appState.scrollOffset;
    appState.outlineLastViewportHeight = surfaceHeight;
}

bool ScrollOutlineBy(AppState& appState, float delta, float surfaceHeight, float contentTopInset) {
    const float previousOffset = appState.outlineScrollOffset;
    appState.outlineScrollOffset = std::clamp(
        appState.outlineScrollOffset - delta,
        0.0f,
        GetMaxOutlineScroll(appState, surfaceHeight, contentTopInset));
    if (std::abs(previousOffset - appState.outlineScrollOffset) < 0.01f) {
        return false;
    }
    appState.needsRepaint = true;
    return true;
}

std::optional<SkRect> GetOutlineScrollbarThumbRect(
    const AppState& appState,
    float surfaceWidth,
    float surfaceHeight,
    float contentTopInset) {
    if (appState.outlineCollapsed || appState.docLayout.outline.empty()) {
        return std::nullopt;
    }

    const float viewportHeight = GetOutlineViewportHeight(surfaceHeight, contentTopInset);
    const float contentHeight = static_cast<float>(appState.docLayout.outline.size()) * kOutlineItemHeight;
    const float maxScroll = GetMaxOutlineScroll(appState, surfaceHeight, contentTopInset);
    if (viewportHeight <= 0.0f || contentHeight <= viewportHeight || maxScroll <= 0.0f) {
        return std::nullopt;
    }

    const float trackTop = contentTopInset + kOutlineTopPadding;
    const float thumbHeight = std::min(
        std::max(viewportHeight * (viewportHeight / contentHeight), 24.0f),
        viewportHeight);
    const float maxThumbTravel = std::max(viewportHeight - thumbHeight, 0.0f);
    const float thumbY = trackTop + (appState.outlineScrollOffset / maxScroll) * maxThumbTravel;
    const float thumbX = GetOutlineX(appState, surfaceWidth) + GetOutlineSidebarWidth(appState) -
        kOutlineScrollbarMargin - kOutlineScrollbarWidth;
    return SkRect::MakeXYWH(thumbX, thumbY, kOutlineScrollbarWidth, thumbHeight);
}

void BeginOutlineScrollbarDrag(AppState& appState, float dragOffset) {
    appState.isDraggingOutlineScrollbar = true;
    appState.outlineScrollbarDragOffset = std::max(dragOffset, 0.0f);
    appState.needsRepaint = true;
}

bool UpdateOutlineScrollFromThumb(
    AppState& appState,
    float pointerY,
    float surfaceHeight,
    float contentTopInset) {
    const float viewportHeight = GetOutlineViewportHeight(surfaceHeight, contentTopInset);
    const float contentHeight = static_cast<float>(appState.docLayout.outline.size()) * kOutlineItemHeight;
    const float maxScroll = GetMaxOutlineScroll(appState, surfaceHeight, contentTopInset);
    if (viewportHeight <= 0.0f || contentHeight <= viewportHeight || maxScroll <= 0.0f) {
        return false;
    }

    const float thumbHeight = std::min(
        std::max(viewportHeight * (viewportHeight / contentHeight), 24.0f),
        viewportHeight);
    const float maxThumbTravel = std::max(viewportHeight - thumbHeight, 0.0f);
    if (maxThumbTravel <= 0.0f) {
        return false;
    }

    const float trackTop = contentTopInset + kOutlineTopPadding;
    const float thumbTop = std::clamp(
        pointerY - appState.outlineScrollbarDragOffset - trackTop,
        0.0f,
        maxThumbTravel);
    const float previousOffset = appState.outlineScrollOffset;
    appState.outlineScrollOffset = (thumbTop / maxThumbTravel) * maxScroll;
    appState.needsRepaint = true;
    return std::abs(previousOffset - appState.outlineScrollOffset) >= 0.01f;
}

void EndOutlinePointerDrag(AppState& appState) {
    appState.isDraggingOutlineScrollbar = false;
    appState.outlineScrollbarDragOffset = 0.0f;
    appState.isResizingOutline = false;
}

size_t GetCurrentOutlineIndex(const DocumentLayout& layout, float visibleDocumentTop) {
    size_t currentIndex = 0;
    bool found = false;
    for (size_t index = 0; index < layout.outline.size(); ++index) {
        if (layout.outline[index].y <= visibleDocumentTop + 4.0f) {
            currentIndex = index;
            found = true;
        } else {
            break;
        }
    }
    return found ? currentIndex : 0;
}

bool FocusOutlineItem(AppState& appState, size_t index, float maxScroll) {
    if (appState.docLayout.outline.empty()) {
        return false;
    }

    const size_t clampedIndex = std::min(index, appState.docLayout.outline.size() - 1);
    appState.outlineFocused = true;
    appState.focusedOutlineIndex = clampedIndex;
    appState.scrollOffset = std::clamp(appState.docLayout.outline[clampedIndex].y, 0.0f, maxScroll);
    ClearRelayoutScrollAnchor(appState);
    appState.needsRepaint = true;
    return true;
}

bool MoveOutlineFocus(AppState& appState, int direction, float maxScroll) {
    if (!appState.outlineFocused || appState.outlineCollapsed || appState.docLayout.outline.empty() || direction == 0) {
        return false;
    }

    const size_t currentIndex = std::min(
        appState.focusedOutlineIndex,
        appState.docLayout.outline.size() - 1);
    size_t nextIndex = currentIndex;
    if (direction < 0) {
        nextIndex = currentIndex == 0 ? 0 : currentIndex - 1;
    } else {
        nextIndex = std::min(currentIndex + 1, appState.docLayout.outline.size() - 1);
    }

    return FocusOutlineItem(appState, nextIndex, maxScroll);
}

bool HitTestOutlineToggle(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset) {
    if (GetOutlineSidebarWidth(appState) <= 0.0f) {
        return false;
    }
    return GetOutlineToggleRect(appState, surfaceWidth, contentTopInset).contains(x, y);
}

std::optional<size_t> HitTestOutlineSidebar(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset) {
    const float outlineX = GetOutlineX(appState, surfaceWidth);
    const float localX = x - outlineX;
    if (appState.outlineCollapsed ||
        appState.docLayout.outline.empty() ||
        localX < 0.0f ||
        localX >= GetOutlineSidebarWidth(appState) ||
        y < contentTopInset) {
        return std::nullopt;
    }

    const float localY = y - contentTopInset - kOutlineTopPadding +
        appState.outlineScrollOffset;
    if (localY < 0.0f) {
        return std::nullopt;
    }

    const auto index = static_cast<size_t>(localY / kOutlineItemHeight);
    if (index >= appState.docLayout.outline.size()) {
        return std::nullopt;
    }

    return index;
}

} // namespace mdviewer
