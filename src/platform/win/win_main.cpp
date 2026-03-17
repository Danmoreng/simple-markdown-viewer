#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string>
#include <filesystem>
#include <iostream>

#include "app/app_state.h"
#include "util/file_io.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) // conversion from 'int32_t' to 'float', possible loss of data
#pragma warning(disable: 4267) // conversion from 'size_t' to 'int', possible loss of data
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColor.h"
#pragma warning(pop)

namespace {
    mdviewer::AppState g_appState;
}

void LoadFile(HWND hwnd, const std::filesystem::path& path) {
    auto content = mdviewer::ReadFileToString(path);
    if (content) {
        g_appState.SetFile(path, std::move(*content));
        
        // Update window title
        std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
        SetWindowTextW(hwnd, title.c_str());

        InvalidateRect(hwnd, NULL, TRUE);
    } else {
        MessageBoxW(hwnd, L"Could not load file.", L"Error", MB_ICONERROR);
    }
}

void OnDropFiles(HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (count > 0) {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            LoadFile(hwnd, path);
        }
    }
    DragFinish(hDrop);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // For now, simple background clear
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
            
            // Placeholder text if no file loaded
            if (g_appState.sourceText.empty()) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                DrawTextW(hdc, L"Drag and drop a Markdown file here", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                // Skia rendering will go here later
                RECT rect;
                GetClientRect(hwnd, &rect);
                std::wstring msg = L"Loaded: " + g_appState.currentFilePath.filename().wstring();
                DrawTextW(hdc, msg.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DROPFILES:
            OnDropFiles(hwnd, (HDROP)wParam);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Markdown Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Process command line
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            LoadFile(hwnd, argv[1]);
        }
        LocalFree(argv);
    }

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
