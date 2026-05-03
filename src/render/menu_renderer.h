#pragma once

#include <optional>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "render/theme.h"

namespace mdviewer {

struct MenuBarItem {
    std::string label;
    int id;
};

struct MenuBarState {
    int hoveredIndex = -1;
    int activeIndex = -1;
    int hoveredItemIndex = -1;
    bool canGoBack = false;
    bool canGoForward = false;
    bool canZoomIn = false;
    bool canZoomOut = false;
};

struct DropdownItem {
    std::string label;
    bool isSeparator = false;
};

enum class MenuBarHitTarget {
    None,
    MenuItem,
    GoBack,
    GoForward,
    ZoomOut,
    ZoomIn
};

struct MenuBarHitTestResult {
    MenuBarHitTarget target = MenuBarHitTarget::None;
    int menuIndex = -1;

    [[nodiscard]] bool HasHit() const { return target != MenuBarHitTarget::None; }
};

struct MenuBarLayout {
    SkRect bounds = SkRect::MakeEmpty();
    std::vector<SkRect> itemRects;
    SkRect zoomOutRect = SkRect::MakeEmpty();
    SkRect zoomInRect = SkRect::MakeEmpty();
    SkRect backRect = SkRect::MakeEmpty();
    SkRect forwardRect = SkRect::MakeEmpty();
};

std::vector<float> MeasureMenuBarItemWidths(const std::vector<MenuBarItem>& items, SkTypeface* typeface);
MenuBarLayout ComputeMenuBarLayout(float width, float height, const std::vector<float>& itemWidths);
MenuBarHitTestResult HitTestMenuBarLayout(const MenuBarLayout& layout, float x, float y);
int MenuBarStateIndexFromHit(const MenuBarHitTestResult& hit);
float MeasureDropdownWidth(const std::vector<DropdownItem>& items, SkTypeface* typeface);
SkRect ComputeDropdownLayout(float x, float y, const std::vector<DropdownItem>& items, SkTypeface* typeface);
int HitTestDropdownLayout(const SkRect& dropdownRect, float itemHeight, float x, float y);

void DrawMenuBar(
    SkCanvas& canvas,
    float width,
    float height,
    const std::vector<MenuBarItem>& items,
    const MenuBarState& state,
    SkTypeface* typeface,
    const ThemePalette& palette);

void DrawDropdown(
    SkCanvas& canvas,
    float x,
    float y,
    const std::vector<DropdownItem>& items,
    int hoveredItemIndex,
    SkTypeface* typeface,
    const ThemePalette& palette);

void DrawArrow(SkCanvas& canvas, float cx, float cy, float size, bool pointingRight, SkColor color);
void DrawZoomGlyph(SkCanvas& canvas, float cx, float cy, float size, bool isPlus, SkColor color);

} // namespace mdviewer
