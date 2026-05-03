#pragma once

#include <optional>

#include "app/app_state.h"
#include "layout/layout_engine.h"

namespace mdviewer {

inline constexpr float kOutlineSidebarWidth = 260.0f;
inline constexpr float kOutlineCollapsedWidth = 34.0f;
inline constexpr float kOutlineItemHeight = 32.0f;
inline constexpr float kOutlineTopPadding = 8.0f;
inline constexpr float kOutlineHeaderHeight = 34.0f;

float GetOutlineSidebarWidth(const AppState& appState);
size_t GetCurrentOutlineIndex(const DocumentLayout& layout, float visibleDocumentTop);
bool FocusOutlineItem(AppState& appState, size_t index, float maxScroll);
bool MoveOutlineFocus(AppState& appState, int direction, float maxScroll);
bool HitTestOutlineToggle(
    const AppState& appState,
    float x,
    float y,
    float contentTopInset);
std::optional<size_t> HitTestOutlineSidebar(
    const AppState& appState,
    float x,
    float y,
    float contentTopInset);

} // namespace mdviewer
