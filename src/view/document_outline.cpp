#include "view/document_outline.h"

#include <algorithm>

#include "view/document_interaction.h"

namespace mdviewer {

float GetOutlineSidebarWidth(const AppState& appState) {
    if (appState.docLayout.outline.empty()) {
        return 0.0f;
    }
    return appState.outlineCollapsed ? kOutlineCollapsedWidth : kOutlineSidebarWidth;
}

float GetOutlineX(const AppState& appState, float surfaceWidth) {
    const float width = GetOutlineSidebarWidth(appState);
    if (width <= 0.0f || appState.outlineSide == OutlineSide::Left) {
        return 0.0f;
    }
    return std::max(surfaceWidth - width, 0.0f);
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
    const float width = GetOutlineSidebarWidth(appState);
    if (width <= 0.0f || y < contentTopInset + 5.0f || y >= contentTopInset + 29.0f) {
        return false;
    }

    const float outlineX = GetOutlineX(appState, surfaceWidth);
    const float localX = x - outlineX;
    const float expandedToggleX = appState.outlineSide == OutlineSide::Right ? 6.0f : std::max(width - 30.0f, 6.0f);
    const float toggleX = appState.outlineCollapsed ? 5.0f : expandedToggleX;
    return localX >= toggleX && localX < toggleX + 24.0f;
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
        localX >= kOutlineSidebarWidth ||
        y < contentTopInset) {
        return std::nullopt;
    }

    const float localY = y - contentTopInset - kOutlineHeaderHeight - kOutlineTopPadding;
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
