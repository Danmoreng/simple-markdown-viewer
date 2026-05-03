#include "view/document_interaction.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mdviewer {
namespace {

std::string ToLowerAscii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

void AppendMatchesInText(AppState& appState, const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return;
    }

    size_t offset = 0;
    while (offset < haystack.size()) {
        const size_t found = haystack.find(needle, offset);
        if (found == std::string::npos) {
            break;
        }
        appState.searchMatches.push_back({found, found + appState.searchQuery.size()});
        offset = found + std::max<size_t>(needle.size(), 1);
    }
}

std::optional<float> FindTextPositionY(const std::vector<BlockLayout>& blocks, size_t textPosition) {
    for (const auto& block : blocks) {
        for (const auto& line : block.lines) {
            if (textPosition >= line.textStart && textPosition <= line.textStart + line.textLength) {
                return line.y;
            }
        }

        if (const auto childY = FindTextPositionY(block.children, textPosition)) {
            return childY;
        }
    }

    return std::nullopt;
}

struct LineAnchorCandidate {
    size_t textPosition = 0;
    float y = 0.0f;
    float height = 0.0f;
    float distance = 0.0f;
    bool valid = false;
};

void FindAnchorLineNearY(
    const std::vector<BlockLayout>& blocks,
    float documentY,
    LineAnchorCandidate& bestCandidate) {
    for (const auto& block : blocks) {
        for (const auto& line : block.lines) {
            const float lineBottom = line.y + std::max(line.height, 1.0f);
            float distance = 0.0f;
            if (documentY < line.y) {
                distance = line.y - documentY;
            } else if (documentY >= lineBottom) {
                distance = documentY - lineBottom;
            }

            const bool betterTie =
                bestCandidate.valid &&
                std::abs(distance - bestCandidate.distance) < 0.001f &&
                bestCandidate.y < documentY &&
                line.y >= documentY;
            if (!bestCandidate.valid || distance < bestCandidate.distance || betterTie) {
                bestCandidate = LineAnchorCandidate{
                    line.textStart,
                    line.y,
                    line.height,
                    distance,
                    true,
                };
            }
        }

        FindAnchorLineNearY(block.children, documentY, bestCandidate);
    }
}

void FindLineForTextPosition(
    const std::vector<BlockLayout>& blocks,
    size_t textPosition,
    LineAnchorCandidate& bestCandidate) {
    for (const auto& block : blocks) {
        for (const auto& line : block.lines) {
            const size_t lineEnd = line.textStart + line.textLength;
            const bool contains = textPosition >= line.textStart && textPosition <= lineEnd;
            const size_t distance =
                textPosition < line.textStart
                    ? line.textStart - textPosition
                    : (textPosition > lineEnd ? textPosition - lineEnd : 0);

            if (contains || !bestCandidate.valid || static_cast<float>(distance) < bestCandidate.distance) {
                bestCandidate = LineAnchorCandidate{
                    line.textStart,
                    line.y,
                    line.height,
                    static_cast<float>(distance),
                    true,
                };
                if (contains) {
                    return;
                }
            }
        }

        FindLineForTextPosition(block.children, textPosition, bestCandidate);
        if (bestCandidate.valid && bestCandidate.distance == 0.0f) {
            return;
        }
    }
}

} // namespace

size_t GetSelectionStart(const AppState& appState) {
    return std::min(appState.selectionAnchor, appState.selectionFocus);
}

size_t GetSelectionEnd(const AppState& appState) {
    return std::max(appState.selectionAnchor, appState.selectionFocus);
}

std::optional<std::pair<size_t, size_t>> GetCurrentSearchMatch(const AppState& appState) {
    if (!appState.searchActive || appState.searchMatches.empty() || appState.currentSearchMatch >= appState.searchMatches.size()) {
        return std::nullopt;
    }
    return appState.searchMatches[appState.currentSearchMatch];
}

ScrollAnchor CaptureScrollAnchor(const DocumentLayout& layout, float scrollOffset, float viewportHeight) {
    ScrollAnchor anchor;
    const float maxScroll = std::max(layout.totalHeight - viewportHeight, 0.0f);
    anchor.scrollRatio = maxScroll > 0.0f ? std::clamp(scrollOffset / maxScroll, 0.0f, 1.0f) : 0.0f;

    LineAnchorCandidate candidate;
    FindAnchorLineNearY(layout.blocks, scrollOffset, candidate);
    if (!candidate.valid) {
        return anchor;
    }

    anchor.textPosition = candidate.textPosition;
    anchor.lineOffsetRatio = candidate.height > 0.0f
        ? std::clamp((scrollOffset - candidate.y) / candidate.height, 0.0f, 1.0f)
        : 0.0f;
    anchor.valid = true;
    return anchor;
}

ScrollAnchor GetRelayoutScrollAnchor(AppState& appState, float viewportHeight) {
    if (appState.relayoutScrollAnchor.has_value()) {
        return *appState.relayoutScrollAnchor;
    }

    return CaptureScrollAnchor(appState.docLayout, appState.scrollOffset, viewportHeight);
}

void RememberRelayoutScrollAnchor(AppState& appState, const ScrollAnchor& anchor) {
    appState.relayoutScrollAnchor = anchor;
}

void ClearRelayoutScrollAnchor(AppState& appState) {
    appState.relayoutScrollAnchor.reset();
}

void RestoreScrollAnchor(AppState& appState, const ScrollAnchor& anchor, float viewportHeight, float maxScroll) {
    (void)viewportHeight;
    if (!anchor.valid) {
        appState.scrollOffset = std::clamp(anchor.scrollRatio * maxScroll, 0.0f, maxScroll);
        appState.needsRepaint = true;
        return;
    }

    LineAnchorCandidate candidate;
    FindLineForTextPosition(appState.docLayout.blocks, anchor.textPosition, candidate);
    if (candidate.valid) {
        const float lineOffset = std::clamp(anchor.lineOffsetRatio, 0.0f, 1.0f) * std::max(candidate.height, 1.0f);
        appState.scrollOffset = std::clamp(candidate.y + lineOffset, 0.0f, maxScroll);
    } else {
        appState.scrollOffset = std::clamp(anchor.scrollRatio * maxScroll, 0.0f, maxScroll);
    }
    appState.needsRepaint = true;
}

void RebuildSearchMatches(AppState& appState) {
    appState.searchMatches.clear();
    appState.currentSearchMatch = 0;

    if (appState.searchQuery.empty()) {
        return;
    }

    AppendMatchesInText(appState, ToLowerAscii(appState.docLayout.plainText), ToLowerAscii(appState.searchQuery));
}

void OpenSearch(AppState& appState) {
    appState.searchActive = true;
    appState.searchCloseButtonRect = SkRect::MakeEmpty();
    appState.selectionAnchor = 0;
    appState.selectionFocus = 0;
    RebuildSearchMatches(appState);
    appState.needsRepaint = true;
}

void CloseSearch(AppState& appState) {
    appState.searchActive = false;
    appState.searchQuery.clear();
    appState.searchMatches.clear();
    appState.currentSearchMatch = 0;
    appState.searchCloseButtonRect = SkRect::MakeEmpty();
    appState.needsRepaint = true;
}

void InsertSearchText(AppState& appState, const std::string& text) {
    if (!appState.searchActive || text.empty()) {
        return;
    }

    appState.searchQuery += text;
    RebuildSearchMatches(appState);
    appState.needsRepaint = true;
}

void DeleteLastSearchCharacter(AppState& appState) {
    if (!appState.searchActive || appState.searchQuery.empty()) {
        return;
    }

    size_t eraseFrom = appState.searchQuery.size() - 1;
    while (eraseFrom > 0 &&
           (static_cast<unsigned char>(appState.searchQuery[eraseFrom]) & 0xC0) == 0x80) {
        --eraseFrom;
    }
    appState.searchQuery.erase(eraseFrom);

    RebuildSearchMatches(appState);
    appState.needsRepaint = true;
}

void MoveSearchMatch(AppState& appState, int direction) {
    if (!appState.searchActive || appState.searchMatches.empty() || direction == 0) {
        return;
    }

    const size_t count = appState.searchMatches.size();
    if (direction > 0) {
        appState.currentSearchMatch = (appState.currentSearchMatch + 1) % count;
    } else {
        appState.currentSearchMatch = appState.currentSearchMatch == 0 ? count - 1 : appState.currentSearchMatch - 1;
    }
    appState.needsRepaint = true;
}

bool ScrollToCurrentSearchMatch(AppState& appState, float viewportHeight, float maxScroll) {
    const auto match = GetCurrentSearchMatch(appState);
    if (!match) {
        return false;
    }

    const auto matchY = FindTextPositionY(appState.docLayout.blocks, match->first);
    if (!matchY) {
        return false;
    }

    const float padding = 48.0f;
    const float previousOffset = appState.scrollOffset;
    if (*matchY < appState.scrollOffset + padding) {
        appState.scrollOffset = std::max(*matchY - padding, 0.0f);
    } else if (*matchY > appState.scrollOffset + viewportHeight - padding) {
        appState.scrollOffset = std::min(*matchY - viewportHeight + padding, maxScroll);
    }

    appState.scrollOffset = std::clamp(appState.scrollOffset, 0.0f, maxScroll);
    const bool moved = std::abs(previousOffset - appState.scrollOffset) > 0.01f;
    if (moved) {
        ClearRelayoutScrollAnchor(appState);
    }
    return moved;
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
    ClearRelayoutScrollAnchor(appState);
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
        ClearRelayoutScrollAnchor(appState);
        return;
    }

    const float normalized = trackHeight > 0.0f ? (thumbTop - scrollbarMargin) / trackHeight : 0.0f;
    appState.scrollOffset = normalized * maxScroll;
    ClearRelayoutScrollAnchor(appState);
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
    const bool moved = std::abs(appState.scrollOffset - previousOffset) > 0.01f;
    if (moved) {
        ClearRelayoutScrollAnchor(appState);
    }
    return moved;
}

void ApplyWheelScroll(AppState& appState, float delta, float maxScroll) {
    const float previousOffset = appState.scrollOffset;
    appState.scrollOffset = std::clamp(appState.scrollOffset - delta, 0.0f, maxScroll);
    if (std::abs(appState.scrollOffset - previousOffset) > 0.01f) {
        ClearRelayoutScrollAnchor(appState);
    }
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

    if (event.ctrl && event.key == InteractionKey::Find) {
        result.handled = true;
        result.openSearch = true;
        return result;
    }

    if (event.ctrl && event.shift && event.key == InteractionKey::ToggleOutline) {
        result.handled = true;
        result.toggleOutline = true;
        return result;
    }

    if (appState.searchActive) {
        if (event.key == InteractionKey::Escape) {
            result.handled = true;
            result.closeSearch = true;
            return result;
        }

        if (event.key == InteractionKey::Enter) {
            result.handled = true;
            result.searchNext = !event.shift;
            result.searchPrevious = event.shift;
            return result;
        }

        if (event.key == InteractionKey::Back) {
            result.handled = true;
            result.searchBackspace = true;
            return result;
        }
    }

    if (appState.outlineFocused && !appState.outlineCollapsed && !appState.docLayout.outline.empty()) {
        if (event.key == InteractionKey::Up) {
            result.handled = true;
            result.outlinePrevious = true;
            return result;
        }
        if (event.key == InteractionKey::Down) {
            result.handled = true;
            result.outlineNext = true;
            return result;
        }
    }

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
