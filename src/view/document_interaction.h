#pragma once

#include <string>
#include <optional>
#include <utility>

#include "app/app_state.h"
#include "view/scroll_anchor.h"

namespace mdviewer {

struct InteractionTextHit {
    size_t position = 0;
    bool valid = false;
    std::string url;
};

enum class InteractionKey {
    Unknown,
    Escape,
    ZoomIn,
    ZoomOut,
    Left,
    Right,
    Up,
    Down,
    Back,
    Copy,
    Find,
    ToggleOutline,
    Enter
};

struct KeyEvent {
    InteractionKey key = InteractionKey::Unknown;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct KeyCommandResult {
    bool handled = false;
    bool stopAutoScroll = false;
    bool copySelection = false;
    bool goBack = false;
    bool goForward = false;
    bool zoomIn = false;
    bool zoomOut = false;
    bool openSearch = false;
    bool closeSearch = false;
    bool searchNext = false;
    bool searchPrevious = false;
    bool searchBackspace = false;
    bool outlinePrevious = false;
    bool outlineNext = false;
    bool toggleOutline = false;
};

struct PointerUpResult {
    bool shouldUpdateSelection = false;
    bool activateLink = false;
    std::string linkUrl;
    bool forceExternal = false;
};

size_t GetSelectionStart(const AppState& appState);
size_t GetSelectionEnd(const AppState& appState);
std::optional<std::pair<size_t, size_t>> GetCurrentSearchMatch(const AppState& appState);
ScrollAnchor CaptureScrollAnchor(const DocumentLayout& layout, float scrollOffset, float viewportHeight);
ScrollAnchor GetRelayoutScrollAnchor(AppState& appState, float viewportHeight);
void RememberRelayoutScrollAnchor(AppState& appState, const ScrollAnchor& anchor);
void ClearRelayoutScrollAnchor(AppState& appState);
void RestoreScrollAnchor(AppState& appState, const ScrollAnchor& anchor, float viewportHeight, float maxScroll);

void ClearPendingLinkState(AppState& appState);
void OpenSearch(AppState& appState);
void CloseSearch(AppState& appState);
void InsertSearchText(AppState& appState, const std::string& text);
void DeleteLastSearchCharacter(AppState& appState);
void MoveSearchMatch(AppState& appState, int direction);
void RebuildSearchMatches(AppState& appState);
bool ScrollToCurrentSearchMatch(AppState& appState, float viewportHeight, float maxScroll);
void BeginScrollbarDrag(AppState& appState, float dragOffset);
void BeginSelection(AppState& appState, const InteractionTextHit& hit, bool forceExternal, int pressX, int pressY);

bool UpdateHoveredUrl(AppState& appState, const InteractionTextHit& hit);
void UpdateSelectionFromHit(
    AppState& appState,
    const InteractionTextHit& hit,
    float contentY,
    float viewportHeight,
    float maxScroll);
void UpdateScrollOffsetFromThumb(
    AppState& appState,
    int mouseY,
    const SkRect& thumb,
    float viewportHeight,
    float maxScroll,
    float scrollbarMargin);

bool CancelPendingLinkIfDragged(AppState& appState, int x, int y, int clickSlop);

void StartAutoScrollState(AppState& appState, float x, float y);
void StopAutoScrollState(AppState& appState);
void UpdateAutoScrollCursor(AppState& appState, float x, float y);
bool TickAutoScroll(AppState& appState, float maxScroll, float deadZone);
float ComputeAutoScrollVelocity(float delta, float deadZone);

void ApplyWheelScroll(AppState& appState, float delta, float maxScroll);
PointerUpResult FinishPrimaryPointerInteraction(AppState& appState, const InteractionTextHit& releaseHit);
void FinalizeSelectionInteraction(AppState& appState);

KeyCommandResult HandleKeyDown(const AppState& appState, const KeyEvent& event);

} // namespace mdviewer
