#include "platform/win/win_window.h"

#include <windowsx.h>

#include <limits>
#include <mutex>

#include "platform/win/win_file_dialog.h"
#include "platform/win/win_file_watcher.h"
#include "platform/win/win_interaction.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_message_dialog.h"
#include "platform/win/win_viewer_host.h"
#include "render/typography.h"
#include "view/document_interaction.h"

namespace mdviewer::win {

namespace {

void RememberRestoredWindowPlacement(HWND hwnd, ViewerController& controller) {
    if (IsIconic(hwnd) || IsZoomed(hwnd)) {
        return;
    }

    RECT bounds = {};
    if (!GetWindowRect(hwnd, &bounds)) {
        return;
    }

    const long long width = static_cast<long long>(bounds.right) - bounds.left;
    const long long height = static_cast<long long>(bounds.bottom) - bounds.top;
    if (width <= 0 || height <= 0 ||
        width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max()) {
        return;
    }

    controller.SetWindowPlacement(WindowPlacement{
        bounds.left,
        bounds.top,
        static_cast<int>(width),
        static_cast<int>(height),
    });
}

WindowCommandHandlers MakeWindowCommandHandlers(HWND hwnd, WinApp& app) {
    auto* appPtr = &app;
    return WindowCommandHandlers{
        .openFile = [hwnd, appPtr]() {
            if (const auto path = ShowOpenFileDialog(hwnd)) {
                LoadFile(hwnd, appPtr->Host(), *path);
            }
        },
        .reload = [hwnd, appPtr]() {
            if (!ReloadCurrentFile(hwnd, appPtr->Host(), true)) {
                ShowErrorMessage(hwnd, "Reload failed", "The current document could not be reloaded.");
            }
        },
        .saveAsPdf = [hwnd, appPtr]() {
            if (!appPtr->Controller().HasCurrentFile()) {
                MessageBoxW(hwnd, L"Open a Markdown file before saving as PDF.", L"Save as PDF", MB_ICONINFORMATION);
                return;
            }

            std::filesystem::path currentPath;
            {
                auto& appState = GetAppState(appPtr->Host());
                std::lock_guard<std::mutex> lock(appState.mtx);
                currentPath = appState.currentFilePath;
            }
            if (const auto path = ShowSavePdfDialog(hwnd, currentPath)) {
                SaveCurrentDocumentAsPdf(hwnd, appPtr->Host(), *path);
            }
        },
        .print = [hwnd, appPtr]() {
            PrintCurrentDocument(hwnd, appPtr->Host());
        },
        .openRecentFile = [hwnd, appPtr](const std::filesystem::path& path) {
            LoadFile(hwnd, appPtr->Host(), path);
        },
        .exitApp = [hwnd]() {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        },
        .selectFont = [hwnd, appPtr]() {
            if (const auto familyName = ShowFontDialog(hwnd, appPtr->GetSelectedFontFamily())) {
                ApplySelectedFont(hwnd, appPtr->Host(), WinApp::WideToUtf8(*familyName));
            }
        },
        .useDefaultFont = [hwnd, appPtr]() {
            if (appPtr->Controller().HasCustomFontFamily()) {
                ApplySelectedFont(hwnd, appPtr->Host(), "");
            }
        },
        .applyLightTheme = [hwnd, appPtr]() {
            ApplyTheme(hwnd, appPtr->Host(), ThemeMode::Light);
        },
        .applySepiaTheme = [hwnd, appPtr]() {
            ApplyTheme(hwnd, appPtr->Host(), ThemeMode::Sepia);
        },
        .applyDarkTheme = [hwnd, appPtr]() {
            ApplyTheme(hwnd, appPtr->Host(), ThemeMode::Dark);
        },
        .goBack = [hwnd, appPtr]() {
            GoBack(hwnd, appPtr->Host());
        },
        .goForward = [hwnd, appPtr]() {
            GoForward(hwnd, appPtr->Host());
        },
        .zoomOut = [hwnd, appPtr]() {
            AdjustBaseFontSize(hwnd, appPtr->Host(), -1.0f);
            if (GetAppState(appPtr->Host()).zoomFeedbackTimeout > 0) {
                SetTimer(hwnd, appPtr->Interaction().zoomFeedbackTimerId, 1200, nullptr);
            }
        },
        .zoomIn = [hwnd, appPtr]() {
            AdjustBaseFontSize(hwnd, appPtr->Host(), 1.0f);
            if (GetAppState(appPtr->Host()).zoomFeedbackTimeout > 0) {
                SetTimer(hwnd, appPtr->Interaction().zoomFeedbackTimerId, 1200, nullptr);
            }
        },
        .zoomReset = [hwnd, appPtr]() {
            SetBaseFontSize(hwnd, appPtr->Host(), kDefaultBaseFontSize);
            if (GetAppState(appPtr->Host()).zoomFeedbackTimeout > 0) {
                SetTimer(hwnd, appPtr->Interaction().zoomFeedbackTimerId, 1200, nullptr);
            }
        },
        .find = [hwnd, appPtr]() {
            auto& appState = GetAppState(appPtr->Host());
            std::lock_guard<std::mutex> lock(appState.mtx);
            OpenSearch(appState);
            InvalidateRect(hwnd, nullptr, FALSE);
        },
        .toggleOutline = [hwnd, appPtr]() {
            appPtr->Controller().ToggleOutlineCollapsed();
            RelayoutCurrentDocument(hwnd, appPtr->Host());
            appPtr->Controller().SaveConfig();
            SyncMenuState(hwnd, appPtr->Host());
            InvalidateRect(hwnd, nullptr, FALSE);
        },
        .outlineLeft = [hwnd, appPtr]() {
            if (appPtr->Controller().SetOutlineSide(OutlineSide::Left)) {
                RelayoutCurrentDocument(hwnd, appPtr->Host());
                appPtr->Controller().SaveConfig();
                SyncMenuState(hwnd, appPtr->Host());
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        },
        .outlineRight = [hwnd, appPtr]() {
            if (appPtr->Controller().SetOutlineSide(OutlineSide::Right)) {
                RelayoutCurrentDocument(hwnd, appPtr->Host());
                appPtr->Controller().SaveConfig();
                SyncMenuState(hwnd, appPtr->Host());
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        },
    };
}

} // namespace

bool RegisterMainWindowClass(HINSTANCE instance, WNDPROC windowProc, int appIconResourceId, const wchar_t* className) {
    auto* largeIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(appIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    auto* smallIcon = static_cast<HICON>(LoadImageW(
        instance,
        MAKEINTRESOURCEW(appIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hIcon = largeIcon;
    windowClass.hIconSm = smallIcon ? smallIcon : largeIcon;

    return RegisterClassExW(&windowClass) != 0;
}

HWND CreateMainWindow(
    HINSTANCE instance,
    const wchar_t* className,
    const wchar_t* title,
    int x,
    int y,
    int width,
    int height) {
    return CreateWindowExW(
        0,
        className,
        title,
        WS_OVERLAPPEDWINDOW,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        nullptr);
}

int RunMessageLoop() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

std::optional<LRESULT> DispatchMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, WinApp& app) {
    switch (message) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            app.Host().fileWatcher.Start(hwnd);
            UpdateSurface(hwnd, app.Host());
            SyncMenuState(hwnd, app.Host());
            return 0;
        case WM_DESTROY:
            RememberRestoredWindowPlacement(hwnd, app.Controller());
            app.Controller().SaveConfig();
            StopAutoScroll(hwnd, app.Interaction());
            app.Host().fileWatcher.Stop();
            app.Host().surface.Shutdown();
            CleanupMenus();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            Render(hwnd, app.Host());
            return 0;
        case WM_MEASUREITEM:
            if (HandleMeasureMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            return std::nullopt;
        case WM_DRAWITEM:
            if (HandleDrawMenuItem(
                    reinterpret_cast<DRAWITEMSTRUCT*>(lParam),
                    GetCurrentThemePalette(app.Host()))) {
                return TRUE;
            }
            return std::nullopt;
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES:
            HandleDropFiles(hwnd, app.Interaction(), reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_COMMAND:
            if (DispatchWindowCommand(LOWORD(wParam), MakeWindowCommandHandlers(hwnd, app))) {
                return 0;
            }
            return std::nullopt;
        case WM_LBUTTONDOWN:
            HandlePrimaryButtonDown(hwnd, app.Interaction(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_XBUTTONDOWN:
            HandleXButtonDown(hwnd, app.Interaction(), wParam);
            return TRUE;
        case WM_MOUSEMOVE:
            HandlePointerMove(
                hwnd,
                app.Interaction(),
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            HandlePrimaryButtonUp(hwnd, app.Interaction(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_CONTEXTMENU:
            if (HandleContextMenu(hwnd, app.Interaction(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
                return 0;
            }
            return std::nullopt;
        case WM_MBUTTONDOWN:
            HandleMiddleButtonDown(hwnd, app.Interaction(), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONUP:
            return 0;
        case WM_MOUSEWHEEL:
            HandleMouseWheel(
                hwnd,
                app.Interaction(),
                GET_WHEEL_DELTA_WPARAM(wParam),
                (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0,
                (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0);
            return 0;
        case WM_MOUSEHWHEEL:
            HandleHorizontalMouseWheel(
                hwnd,
                app.Interaction(),
                GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        case WM_KEYDOWN:
            if (HandleKeyDown(hwnd, app.Interaction(), wParam)) {
                return 0;
            }
            return std::nullopt;
        case WM_CHAR:
            if (HandleTextInput(hwnd, app.Interaction(), static_cast<wchar_t>(wParam))) {
                return 0;
            }
            return std::nullopt;
        case WM_TIMER:
            if (HandleTimer(hwnd, app.Interaction(), wParam)) {
                return 0;
            }
            return std::nullopt;
        case WM_CAPTURECHANGED:
            HandleCaptureChanged(hwnd, app.Interaction(), lParam);
            return 0;
        case WM_SIZE:
            RememberRestoredWindowPlacement(hwnd, app.Controller());
            UpdateSurface(hwnd, app.Host());
            RelayoutCurrentDocument(hwnd, app.Host());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ENTERSIZEMOVE:
            app.Host().imageCache.BeginLiveResize();
            return 0;
        case WM_MOVE:
            RememberRestoredWindowPlacement(hwnd, app.Controller());
            return 0;
        case WM_EXITSIZEMOVE:
            app.Host().imageCache.EndLiveResize();
            RememberRestoredWindowPlacement(hwnd, app.Controller());
            UpdateSurface(hwnd, app.Host());
            RelayoutCurrentDocument(hwnd, app.Host());
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kMessageWatchedFileChanged:
            ReloadIfFileChanged(hwnd, app.Host());
            return 0;
        default:
            return std::nullopt;
    }
}

bool DispatchWindowCommand(UINT_PTR commandId, const WindowCommandHandlers& handlers) {
    if (const auto recentFile = GetRecentFileForCommand(commandId)) {
        if (handlers.openRecentFile) {
            handlers.openRecentFile(*recentFile);
        }
        return true;
    }

    switch (commandId) {
        case kCommandOpenFile:
            if (handlers.openFile) {
                handlers.openFile();
            }
            return true;
        case kCommandReload:
            if (handlers.reload) {
                handlers.reload();
            }
            return true;
        case kCommandSaveAsPdf:
            if (handlers.saveAsPdf) {
                handlers.saveAsPdf();
            }
            return true;
        case kCommandPrint:
            if (handlers.print) {
                handlers.print();
            }
            return true;
        case kCommandExit:
            if (handlers.exitApp) {
                handlers.exitApp();
            }
            return true;
        case kCommandSelectFont:
            if (handlers.selectFont) {
                handlers.selectFont();
            }
            return true;
        case kCommandUseDefaultFont:
            if (handlers.useDefaultFont) {
                handlers.useDefaultFont();
            }
            return true;
        case kCommandThemeLight:
            if (handlers.applyLightTheme) {
                handlers.applyLightTheme();
            }
            return true;
        case kCommandThemeSepia:
            if (handlers.applySepiaTheme) {
                handlers.applySepiaTheme();
            }
            return true;
        case kCommandThemeDark:
            if (handlers.applyDarkTheme) {
                handlers.applyDarkTheme();
            }
            return true;
        case kCommandGoBack:
            if (handlers.goBack) {
                handlers.goBack();
            }
            return true;
        case kCommandGoForward:
            if (handlers.goForward) {
                handlers.goForward();
            }
            return true;
        case kCommandZoomOut:
            if (handlers.zoomOut) {
                handlers.zoomOut();
            }
            return true;
        case kCommandZoomIn:
            if (handlers.zoomIn) {
                handlers.zoomIn();
            }
            return true;
        case kCommandZoomReset:
            if (handlers.zoomReset) {
                handlers.zoomReset();
            }
            return true;
        case kCommandFind:
            if (handlers.find) {
                handlers.find();
            }
            return true;
        case kCommandToggleOutline:
            if (handlers.toggleOutline) {
                handlers.toggleOutline();
            }
            return true;
        case kCommandOutlineLeft:
            if (handlers.outlineLeft) {
                handlers.outlineLeft();
            }
            return true;
        case kCommandOutlineRight:
            if (handlers.outlineRight) {
                handlers.outlineRight();
            }
            return true;
        default:
            return false;
    }
}

} // namespace mdviewer::win
