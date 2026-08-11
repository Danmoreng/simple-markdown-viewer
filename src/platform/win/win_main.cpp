#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <limits>

#include "platform/win/win_app.h"
#include "platform/win/win_crash_handler.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_window.h"
#include "render/math_renderer.h"

namespace {
    mdviewer::win::WinApp g_app;

    constexpr int kInitialWindowWidth = 900;
    constexpr int kInitialWindowHeight = 1200;
    constexpr int kAppIconResourceId = 101;

LONG ClampToLong(long long value) {
    return static_cast<LONG>(std::clamp(
        value,
        static_cast<long long>(std::numeric_limits<LONG>::min()),
        static_cast<long long>(std::numeric_limits<LONG>::max())));
}

mdviewer::WindowPlacement ClampWindowPlacementToDisplays(mdviewer::WindowPlacement placement) {
    RECT requestedRect = {
        ClampToLong(placement.x),
        ClampToLong(placement.y),
        ClampToLong(static_cast<long long>(placement.x) + placement.width),
        ClampToLong(static_cast<long long>(placement.y) + placement.height),
    };
    const HMONITOR monitor = MonitorFromRect(&requestedRect, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return placement;
    }

    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return placement;
    }

    const int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    if (workWidth <= 0 || workHeight <= 0) {
        return placement;
    }

    placement.width = std::min(placement.width, workWidth);
    placement.height = std::min(placement.height, workHeight);
    placement.x = std::clamp(
        placement.x,
        static_cast<int>(monitorInfo.rcWork.left),
        static_cast<int>(monitorInfo.rcWork.right) - placement.width);
    placement.y = std::clamp(
        placement.y,
        static_cast<int>(monitorInfo.rcWork.top),
        static_cast<int>(monitorInfo.rcWork.bottom) - placement.height);
    return placement;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (const auto result = mdviewer::win::DispatchMainWindowMessage(hwnd, uMsg, wParam, lParam, g_app)) {
        return *result;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInitResult);
    if (FAILED(comInitResult) && comInitResult != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"COM initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        return 1;
    }

    mdviewer::win::InstallCrashHandler(mdviewer::win::WinApp::GetUserConfigPath().parent_path());
    mdviewer::SetMathResourceRoot(
        mdviewer::win::WinApp::GetLegacyExecutableConfigPath().parent_path() / L"res");

    g_app.Controller().SetConfigPath(mdviewer::win::WinApp::GetUserConfigPath());
    g_app.Controller().SetLegacyConfigPath(mdviewer::win::WinApp::GetLegacyExecutableConfigPath());
    g_app.Controller().LoadConfig();

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";
    if (!mdviewer::win::RegisterMainWindowClass(hInstance, WindowProc, kAppIconResourceId, CLASS_NAME)) {
        MessageBoxW(nullptr, L"Window class registration failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    if (!mdviewer::win::CreateMenus(mdviewer::win::GetCurrentThemePalette(g_app.Host()))) {
        MessageBoxW(nullptr, L"Menu initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    const auto savedPlacement = g_app.Controller().GetWindowPlacement();
    const mdviewer::WindowPlacement placement = savedPlacement
        ? ClampWindowPlacementToDisplays(*savedPlacement)
        : mdviewer::WindowPlacement{CW_USEDEFAULT, CW_USEDEFAULT, kInitialWindowWidth, kInitialWindowHeight};
    HWND hwnd = mdviewer::win::CreateMainWindow(
        hInstance,
        CLASS_NAME,
        L"Markdown Viewer",
        placement.x,
        placement.y,
        placement.width,
        placement.height);

    if (hwnd == NULL) {
        mdviewer::win::CleanupMenus();
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 0;
    }

    if (!mdviewer::win::EnsureFontSystem(g_app.Host())) {
        MessageBoxW(hwnd, L"Font initialization failed. The application cannot render text.", L"Error", MB_ICONERROR);
        DestroyWindow(hwnd);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            mdviewer::win::LoadFile(hwnd, g_app.Host(), argv[1]);
        }
        LocalFree(argv);
    }

    const int exitCode = mdviewer::win::RunMessageLoop();

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return exitCode;
}
