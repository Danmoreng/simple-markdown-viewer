#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <vector>

#include "app/app_state.h"
#include "app/viewer_controller.h"
#include "platform/win/win_clipboard.h"
#include "platform/win/win_file_dialog.h"
#include "platform/win/win_interaction.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_shell.h"
#include "platform/win/win_surface.h"
#include "platform/win/win_viewer_host.h"
#include "platform/win/win_window.h"
#include "render/document_typefaces.h"
#include "render/document_renderer.h"
#include "render/image_cache.h"
#include "render/theme.h"
#include "render/typography.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkImage.h"
#pragma warning(pop)

namespace {
    mdviewer::ViewerController g_viewerController;
    mdviewer::AppState& g_appState = g_viewerController.GetMutableAppState();
    sk_sp<SkSurface> g_surface;
    mdviewer::DocumentTypefaceCache g_typefaces;
    mdviewer::DocumentImageCache g_imageCache;

    std::wstring Utf8ToWide(const std::string& text);
    mdviewer::win::ViewerHostContext& GetHostContext();
    mdviewer::win::ViewerInteractionContext& GetInteractionContext();

    constexpr UINT_PTR kAutoScrollTimerId = 2001;
    constexpr UINT_PTR kCopiedFeedbackTimerId = 2002;
    constexpr UINT kAutoScrollTimerMs = 16;
    constexpr int kInitialWindowWidth = 900;
    constexpr int kInitialWindowHeight = 1200;
    constexpr int kAppIconResourceId = 101;

    std::wstring GetSelectedFontFamily() {
        const std::string& fontFamilyUtf8 = g_viewerController.GetFontFamilyUtf8();
        if (fontFamilyUtf8.empty()) {
            return {};
        }
        return Utf8ToWide(fontFamilyUtf8);
    }

    std::string WideToUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) {
            return {};
        }

        std::string utf8(static_cast<size_t>(utf8Length), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), utf8Length, nullptr, nullptr);
        return utf8;
    }

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) {
            return {};
        }

        const int wideLength = MultiByteToWideChar(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        if (wideLength <= 0) {
            return {};
        }

        std::wstring wide(static_cast<size_t>(wideLength), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), wideLength);
        return wide;
    }

    std::filesystem::path GetExecutableConfigPath() {
        std::array<wchar_t, MAX_PATH> buffer = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::filesystem::path(L"mdviewer.ini");
        }

        std::filesystem::path executablePath(std::wstring(buffer.data(), length));
        return executablePath.parent_path() / L"mdviewer.ini";
    }

    mdviewer::win::ViewerHostContext& GetHostContext() {
        static mdviewer::win::ViewerHostContext context{
            .controller = g_viewerController,
            .surface = g_surface,
            .typefaces = g_typefaces,
            .imageCache = g_imageCache,
        };
        return context;
    }

    mdviewer::win::ViewerInteractionContext& GetInteractionContext() {
        static mdviewer::win::ViewerInteractionContext context{
            .host = GetHostContext(),
            .autoScrollTimerId = kAutoScrollTimerId,
            .copiedFeedbackTimerId = kCopiedFeedbackTimerId,
            .autoScrollTimerMs = kAutoScrollTimerMs,
            .autoScrollDeadZone = 2.0f,
            .linkClickSlop = 4,
        };
        return context;
    }

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            mdviewer::win::UpdateSurface(hwnd, GetHostContext());
            mdviewer::win::SyncMenuState(hwnd, GetHostContext());
            return 0;
        case WM_DESTROY:
            mdviewer::win::StopAutoScroll(hwnd, GetInteractionContext());
            mdviewer::win::CleanupMenus();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            mdviewer::win::Render(hwnd, GetHostContext());
            return 0;
        case WM_MEASUREITEM:
            if (mdviewer::win::HandleMeasureMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (mdviewer::win::HandleDrawMenuItem(
                    reinterpret_cast<DRAWITEMSTRUCT*>(lParam),
                    mdviewer::win::GetCurrentThemePalette(GetHostContext()))) {
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES:
            mdviewer::win::HandleDropFiles(hwnd, GetInteractionContext(), (HDROP)wParam);
            return 0;
        case WM_COMMAND:
            if (mdviewer::win::DispatchWindowCommand(
                    LOWORD(wParam),
                    mdviewer::win::WindowCommandHandlers{
                        .openFile = [&]() {
                    if (const auto path = mdviewer::win::ShowOpenFileDialog(hwnd)) {
                        mdviewer::win::LoadFile(hwnd, GetHostContext(), *path);
                    }
                        },
                        .openRecentFile = [&](const std::filesystem::path& path) {
                            mdviewer::win::LoadFile(hwnd, GetHostContext(), path);
                        },
                        .exitApp = [&]() {
                            PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        },
                        .selectFont = [&]() {
                    if (const auto familyName = mdviewer::win::ShowFontDialog(hwnd, GetSelectedFontFamily())) {
                        mdviewer::win::ApplySelectedFont(hwnd, GetHostContext(), WideToUtf8(*familyName));
                    }
                        },
                        .useDefaultFont = [&]() {
                    if (g_viewerController.HasCustomFontFamily()) {
                        mdviewer::win::ApplySelectedFont(hwnd, GetHostContext(), "");
                    }
                        },
                        .applyLightTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Light);
                        },
                        .applySepiaTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Sepia);
                        },
                        .applyDarkTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Dark);
                        },
                        .goBack = [&]() {
                            mdviewer::win::GoBack(hwnd, GetHostContext());
                        },
                        .goForward = [&]() {
                            mdviewer::win::GoForward(hwnd, GetHostContext());
                        },
                        .zoomOut = [&]() {
                            mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), -1.0f);
                        },
                        .zoomIn = [&]() {
                            mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), 1.0f);
                        },
                    })) {
                return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            mdviewer::win::HandlePrimaryButtonDown(hwnd, GetInteractionContext(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_XBUTTONDOWN:
            mdviewer::win::HandleXButtonDown(hwnd, GetInteractionContext(), wParam);
            return TRUE;
        case WM_MOUSEMOVE: {
            mdviewer::win::HandlePointerMove(
                hwnd,
                GetInteractionContext(),
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_LBUTTONUP: {
            mdviewer::win::HandlePrimaryButtonUp(hwnd, GetInteractionContext(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_MBUTTONDOWN:
            mdviewer::win::HandleMiddleButtonDown(hwnd, GetInteractionContext(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONUP:
            return 0;
        case WM_MOUSEWHEEL:
            mdviewer::win::HandleMouseWheel(hwnd, GetInteractionContext(), GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_KEYDOWN:
            if (mdviewer::win::HandleKeyDown(hwnd, GetInteractionContext(), wParam)) {
                return 0;
            }
            break;
        case WM_TIMER:
            if (mdviewer::win::HandleTimer(hwnd, GetInteractionContext(), wParam)) {
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            mdviewer::win::HandleCaptureChanged(hwnd, GetInteractionContext(), lParam);
            return 0;
        case WM_SIZE: {
            mdviewer::win::UpdateSurface(hwnd, GetHostContext());
            mdviewer::win::RelayoutCurrentDocument(hwnd, GetHostContext());
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
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

    g_viewerController.SetConfigPath(GetExecutableConfigPath());
    g_viewerController.LoadConfig();

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";
    if (!mdviewer::win::RegisterMainWindowClass(hInstance, WindowProc, kAppIconResourceId, CLASS_NAME)) {
        MessageBoxW(nullptr, L"Window class registration failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    if (!mdviewer::win::CreateMenus(mdviewer::win::GetCurrentThemePalette(GetHostContext()))) {
        MessageBoxW(nullptr, L"Menu initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    HWND hwnd = mdviewer::win::CreateMainWindow(
        hInstance,
        CLASS_NAME,
        L"Markdown Viewer",
        kInitialWindowWidth,
        kInitialWindowHeight);

    if (hwnd == NULL) {
        mdviewer::win::CleanupMenus();
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 0;
    }

    if (!mdviewer::win::EnsureFontSystem(GetHostContext())) {
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
            mdviewer::win::LoadFile(hwnd, GetHostContext(), argv[1]);
        }
        LocalFree(argv);
    }

    const int exitCode = mdviewer::win::RunMessageLoop();

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return exitCode;
}
