#pragma once

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
