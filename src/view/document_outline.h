#pragma once

#include <optional>

#include "app/app_state.h"
#include "layout/layout_engine.h"

namespace mdviewer {

inline constexpr float kOutlineSidebarWidth = kDefaultOutlineWidth;
inline constexpr float kOutlineCollapsedWidth = 34.0f;
inline constexpr float kOutlineItemHeight = 32.0f;
inline constexpr float kOutlineTopPadding = 8.0f;
inline constexpr float kOutlineHeaderHeight = 34.0f;
inline constexpr float kOutlineBottomPadding = 8.0f;
inline constexpr float kOutlineResizeHandleWidth = 8.0f;
inline constexpr float kOutlineScrollbarWidth = 4.0f;
inline constexpr float kOutlineScrollbarMargin = 6.0f;

float GetOutlineSidebarWidth(const AppState& appState);
float GetOutlineX(const AppState& appState, float surfaceWidth);
float GetOutlineViewportHeight(float surfaceHeight, float contentTopInset);
float GetMaxOutlineScroll(const AppState& appState, float surfaceHeight, float contentTopInset);
bool IsPointInOutlineSidebar(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset);
bool HitTestOutlineResizeHandle(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float surfaceHeight,
    float contentTopInset);
bool ResizeOutlineSidebar(AppState& appState, float pointerX, float surfaceWidth);
void SyncOutlineScrollToDocument(AppState& appState, float surfaceHeight, float contentTopInset);
bool ScrollOutlineBy(AppState& appState, float delta, float surfaceHeight, float contentTopInset);
std::optional<SkRect> GetOutlineScrollbarThumbRect(
    const AppState& appState,
    float surfaceWidth,
    float surfaceHeight,
    float contentTopInset);
void BeginOutlineScrollbarDrag(AppState& appState, float dragOffset);
bool UpdateOutlineScrollFromThumb(
    AppState& appState,
    float pointerY,
    float surfaceHeight,
    float contentTopInset);
void EndOutlinePointerDrag(AppState& appState);
size_t GetCurrentOutlineIndex(const DocumentLayout& layout, float visibleDocumentTop);
bool FocusOutlineItem(AppState& appState, size_t index, float maxScroll);
bool MoveOutlineFocus(AppState& appState, int direction, float maxScroll);
bool HitTestOutlineToggle(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset);
std::optional<size_t> HitTestOutlineSidebar(
    const AppState& appState,
    float x,
    float y,
    float surfaceWidth,
    float contentTopInset);

} // namespace mdviewer
