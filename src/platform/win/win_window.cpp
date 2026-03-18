#include "platform/win/win_window.h"

#include "platform/win/win_menu.h"

namespace mdviewer::win {

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

HWND CreateMainWindow(HINSTANCE instance, const wchar_t* className, const wchar_t* title, int width, int height) {
    return CreateWindowExW(
        0,
        className,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
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

bool DispatchWindowCommand(UINT_PTR commandId, const WindowCommandHandlers& handlers) {
    switch (commandId) {
        case kCommandOpenFile:
            if (handlers.openFile) {
                handlers.openFile();
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
        default:
            return false;
    }
}

} // namespace mdviewer::win
