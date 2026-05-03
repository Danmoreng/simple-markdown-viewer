#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <optional>
#include <vector>

#include "app/viewer_controller.h"
#include "render/theme.h"
#include "render/menu_renderer.h"

class SkCanvas;
class SkTypeface;

namespace mdviewer::win {

inline constexpr int kMenuBarHeight = 42;

inline constexpr UINT_PTR kCommandOpenFile = 1001;
inline constexpr UINT_PTR kCommandExit = 1002;
inline constexpr UINT_PTR kCommandSelectFont = 1003;
inline constexpr UINT_PTR kCommandUseDefaultFont = 1004;
inline constexpr UINT_PTR kCommandThemeLight = 1101;
inline constexpr UINT_PTR kCommandThemeSepia = 1102;
inline constexpr UINT_PTR kCommandThemeDark = 1103;
inline constexpr UINT_PTR kCommandGoBack = 1201;
inline constexpr UINT_PTR kCommandGoForward = 1202;
inline constexpr UINT_PTR kCommandZoomOut = 1203;
inline constexpr UINT_PTR kCommandZoomIn = 1204;
inline constexpr UINT_PTR kCommandFind = 1205;

bool CreateMenus(const ThemePalette& palette);
void CleanupMenus();
void UpdateMenuState(
    HWND hwnd,
    ThemeMode currentTheme,
    bool hasCustomFont,
    const ThemePalette& palette,
    const std::vector<RecentFileEntry>& recentFiles);
std::optional<std::filesystem::path> GetRecentFileForCommand(UINT_PTR commandId);

bool HandleMeasureMenuItem(MEASUREITEMSTRUCT* measureInfo);
bool HandleDrawMenuItem(const DRAWITEMSTRUCT* drawInfo, const ThemePalette& palette);

MenuBarHitTestResult HitTestTopMenu(HWND hwnd, int x, int y, int surfaceWidth);
bool UpdateTopMenuHover(HWND hwnd, int x, int y, int surfaceWidth);
UINT OpenTopMenu(HWND hwnd, const MenuBarHitTestResult& hit);

void DrawTopMenuBar(
    SkCanvas* canvas,
    HWND hwnd,
    int surfaceWidth,
    SkTypeface* typeface,
    const ThemePalette& palette,
    bool canGoBack,
    bool canGoForward,
    bool canZoomOut,
    bool canZoomIn);

} // namespace mdviewer::win
