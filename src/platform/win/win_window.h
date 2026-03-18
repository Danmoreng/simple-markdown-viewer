#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>

namespace mdviewer::win {

struct WindowCommandHandlers {
    std::function<void()> openFile;
    std::function<void(const std::filesystem::path&)> openRecentFile;
    std::function<void()> exitApp;
    std::function<void()> selectFont;
    std::function<void()> useDefaultFont;
    std::function<void()> applyLightTheme;
    std::function<void()> applySepiaTheme;
    std::function<void()> applyDarkTheme;
    std::function<void()> goBack;
    std::function<void()> goForward;
    std::function<void()> zoomOut;
    std::function<void()> zoomIn;
};

bool RegisterMainWindowClass(HINSTANCE instance, WNDPROC windowProc, int appIconResourceId, const wchar_t* className);
HWND CreateMainWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, int width, int height);
int RunMessageLoop();

bool DispatchWindowCommand(UINT_PTR commandId, const WindowCommandHandlers& handlers);

} // namespace mdviewer::win
