#include "view/document_interaction.h"

#include <algorithm>
#include <cmath>

namespace mdviewer {

size_t GetSelectionStart(const AppState& appState) {
    return std::min(appState.selectionAnchor, appState.selectionFocus);
}

size_t GetSelectionEnd(const AppState& appState) {
    return std::max(appState.selectionAnchor, appState.selectionFocus);
}

void ClearPendingLinkState(AppState& appState) {
    appState.pendingLinkClick = false;
    appState.pendingLinkForceExternal = false;
    appState.pendingLinkPressX = 0;
    appState.pendingLinkPressY = 0;
    appState.pendingLinkUrl.clear();
}

void BeginScrollbarDrag(AppState& appState, float dragOffset) {
    appState.isDraggingScrollbar = true;
    appState.isSelecting = false;
    appState.scrollbarDragOffset = dragOffset;
    ClearPendingLinkState(appState);
}

void BeginSelection(AppState& appState, const InteractionTextHit& hit, bool forceExternal, int pressX, int pressY) {
    appState.isSelecting = true;
    appState.isDraggingScrollbar = false;
    if (hit.valid) {
        appState.selectionAnchor = hit.position;
        appState.selectionFocus = hit.position;
    } else {
        appState.selectionAnchor = 0;
        appState.selectionFocus = 0;
    }
    appState.pendingLinkClick = hit.valid && !hit.url.empty();
    appState.pendingLinkForceExternal = forceExternal;
    appState.pendingLinkPressX = pressX;
    appState.pendingLinkPressY = pressY;
    appState.pendingLinkUrl = appState.pendingLinkClick ? hit.url : "";
}

bool UpdateHoveredUrl(AppState& appState, const InteractionTextHit& hit) {
    const std::string nextUrl = (hit.valid && !hit.url.empty()) ? hit.url : "";
    if (appState.hoveredUrl == nextUrl) {
        return false;
    }

    appState.hoveredUrl = nextUrl;
    appState.needsRepaint = true;
    return true;
}

void UpdateSelectionFromHit(
    AppState& appState,
    const InteractionTextHit& hit,
    float contentY,
    float viewportHeight,
    float maxScroll) {
    if (hit.valid) {
        appState.selectionFocus = hit.position;
    }

    if (contentY < 0.0f) {
        appState.scrollOffset = std::max(appState.scrollOffset + contentY, 0.0f);
    } else if (contentY > viewportHeight) {
        appState.scrollOffset = std::min(appState.scrollOffset + (contentY - viewportHeight), maxScroll);
    }
}

void UpdateScrollOffsetFromThumb(
    AppState& appState,
    int mouseY,
    const SkRect& thumb,
    float viewportHeight,
    float maxScroll,
    float scrollbarMargin) {
    const float thumbHeight = thumb.height();
    const float trackHeight = viewportHeight - thumbHeight - (scrollbarMargin * 2.0f);
    const float thumbTop = std::clamp(
        static_cast<float>(mouseY) - appState.scrollbarDragOffset,
        scrollbarMargin,
        scrollbarMargin + std::max(trackHeight, 0.0f));

    if (maxScroll <= 0.0f) {
        appState.scrollOffset = 0.0f;
        return;
    }

    const float normalized = trackHeight > 0.0f ? (thumbTop - scrollbarMargin) / trackHeight : 0.0f;
    appState.scrollOffset = normalized * maxScroll;
}

bool CancelPendingLinkIfDragged(AppState& appState, int x, int y, int clickSlop) {
    if (!appState.pendingLinkClick) {
        return false;
    }

    const int dx = x - appState.pendingLinkPressX;
    const int dy = y - appState.pendingLinkPressY;
    if ((dx * dx) + (dy * dy) <= (clickSlop * clickSlop)) {
        return false;
    }

    ClearPendingLinkState(appState);
    return true;
}

void StartAutoScrollState(AppState& appState, float x, float y) {
    appState.isAutoScrolling = true;
    appState.isSelecting = false;
    appState.isDraggingScrollbar = false;
    appState.autoScrollOriginX = x;
    appState.autoScrollOriginY = y;
    appState.autoScrollCursorX = x;
    appState.autoScrollCursorY = y;
}

void StopAutoScrollState(AppState& appState) {
    appState.isAutoScrolling = false;
    appState.autoScrollOriginX = 0.0f;
    appState.autoScrollOriginY = 0.0f;
    appState.autoScrollCursorX = 0.0f;
    appState.autoScrollCursorY = 0.0f;
}

void UpdateAutoScrollCursor(AppState& appState, float x, float y) {
    appState.autoScrollCursorX = x;
    appState.autoScrollCursorY = y;
}

float ComputeAutoScrollVelocity(float delta, float deadZone) {
    const float magnitude = std::abs(delta);
    if (magnitude <= deadZone) {
        return 0.0f;
    }

    const float adjusted = magnitude - deadZone;
    const float normalized = std::min(adjusted / 16.0f, 25.0f);
    const float speed = (normalized * normalized) * 0.55f + (adjusted * 0.22f);
    return delta < 0.0f ? -speed : speed;
}

bool TickAutoScroll(AppState& appState, float maxScroll, float deadZone) {
    if (!appState.isAutoScrolling) {
        return false;
    }

    const float deltaY = appState.autoScrollCursorY - appState.autoScrollOriginY;
    const float scrollDelta = ComputeAutoScrollVelocity(deltaY, deadZone);
    if (scrollDelta == 0.0f) {
        return false;
    }

    const float previousOffset = appState.scrollOffset;
    appState.scrollOffset = std::clamp(appState.scrollOffset + scrollDelta, 0.0f, maxScroll);
    return std::abs(appState.scrollOffset - previousOffset) > 0.01f;
}

void ApplyWheelScroll(AppState& appState, float delta, float maxScroll) {
    appState.scrollOffset = std::clamp(appState.scrollOffset - delta, 0.0f, maxScroll);
}

PointerUpResult FinishPrimaryPointerInteraction(AppState& appState, const InteractionTextHit& releaseHit) {
    PointerUpResult result;

    appState.isDraggingScrollbar = false;
    if (appState.isSelecting) {
        if (appState.pendingLinkClick) {
            result.activateLink = releaseHit.valid &&
                                  !releaseHit.url.empty() &&
                                  releaseHit.url == appState.pendingLinkUrl;
            result.forceExternal = appState.pendingLinkForceExternal;
            result.linkUrl = appState.pendingLinkUrl;
            if (result.activateLink) {
                appState.selectionAnchor = 0;
                appState.selectionFocus = 0;
            } else {
                result.shouldUpdateSelection = true;
            }
        } else {
            result.shouldUpdateSelection = true;
        }
    }

    ClearPendingLinkState(appState);
    return result;
}

void FinalizeSelectionInteraction(AppState& appState) {
    appState.isSelecting = false;
    if (!appState.HasSelection()) {
        appState.selectionAnchor = 0;
        appState.selectionFocus = 0;
    }
}

KeyCommandResult HandleKeyDown(const AppState& appState, const KeyEvent& event) {
    KeyCommandResult result;

    if (event.key == InteractionKey::Escape) {
        result.handled = true;
        result.stopAutoScroll = true;
        return result;
    }

    if (event.ctrl && event.key == InteractionKey::ZoomIn) {
        result.handled = true;
        result.zoomIn = true;
        return result;
    }

    if (event.ctrl && event.key == InteractionKey::ZoomOut) {
        result.handled = true;
        result.zoomOut = true;
        return result;
    }

    if (event.key == InteractionKey::Back) {
        result.handled = true;
        result.goBack = true;
        return result;
    }

    if (event.alt && event.key == InteractionKey::Left) {
        result.handled = true;
        result.goBack = true;
        return result;
    }

    if (event.alt && event.key == InteractionKey::Right) {
        result.handled = true;
        result.goForward = true;
        return result;
    }

    if (!appState.HasSelection() && event.key == InteractionKey::Left) {
        result.handled = true;
        result.goBack = true;
        return result;
    }

    if (!appState.HasSelection() && event.key == InteractionKey::Right) {
        result.handled = true;
        result.goForward = true;
        return result;
    }

    if (event.ctrl && event.key == InteractionKey::Copy) {
        result.handled = true;
        result.copySelection = true;
        return result;
    }

    return result;
}

} // namespace mdviewer
