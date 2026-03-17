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
#include <vector>

#include "app/app_state.h"
#include "util/file_io.h"
#include "markdown/markdown_parser.h"
#include "layout/layout_engine.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace {
    mdviewer::AppState g_appState;
    sk_sp<SkSurface> g_surface;
}

void UpdateSurface(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) return;

    SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    g_surface = SkSurfaces::Raster(info);
}

void Render(HWND hwnd) {
    if (!g_surface) return;

    SkCanvas* canvas = g_surface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        
        SkPaint paint;
        paint.setAntiAlias(true);

        for (const auto& block : g_appState.docLayout.blocks) {
            // Draw block background for debug
            // paint.setColor(SkColorSetARGB(30, 0, 0, 255));
            // canvas->drawRect(block.bounds, paint);

            for (const auto& line : block.lines) {
                float currentX = block.bounds.left();
                for (const auto& run : line.runs) {
                    SkFont font;
                    float fontSize = 16.0f;
                    if (block.type == mdviewer::BlockType::Heading1) fontSize = 32.0f;
                    else if (block.type == mdviewer::BlockType::Heading2) fontSize = 24.0f;
                    
                    font.setSize(fontSize);
                    if (run.style == mdviewer::InlineStyle::Strong) {
                        // font.setEmbolden(true); // Simplified
                    }

                    paint.setColor(SK_ColorBLACK);
                    canvas->drawString(run.text.c_str(), currentX, line.y + line.height - 5.0f, font, paint);
                    currentX += font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                }
            }
        }

        if (g_appState.sourceText.empty()) {
            SkFont font;
            font.setSize(20.0f);
            paint.setColor(SK_ColorGRAY);
            const char* msg = "Drag and drop a Markdown file here";
            SkRect bounds;
            font.measureText(msg, strlen(msg), SkTextEncoding::kUTF8, &bounds);
            canvas->drawString(msg, (g_surface->width() - bounds.width()) / 2, g_surface->height() / 2, font, paint);
        }
    }

    // Flush surface and copy to window
    SkPixmap pixmap;
    if (g_surface->peekPixels(&pixmap)) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pixmap.width();
        bmi.bmiHeader.biHeight = -pixmap.height(); // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(hdc, 0, 0, pixmap.width(), pixmap.height(),
                      0, 0, pixmap.width(), pixmap.height(),
                      pixmap.addr(), &bmi, DIB_RGB_COLORS, SRCCOPY);

        EndPaint(hwnd, &ps);
    }
}

void LoadFile(HWND hwnd, const std::filesystem::path& path) {
    auto content = mdviewer::ReadFileToString(path);
    if (content) {
        auto docModel = mdviewer::MarkdownParser::Parse(*content);
        
        RECT rect;
        GetClientRect(hwnd, &rect);
        float width = static_cast<float>(rect.right - rect.left);
        auto layout = mdviewer::LayoutEngine::ComputeLayout(docModel, width);

        g_appState.SetFile(path, std::move(*content), std::move(docModel), std::move(layout));
        
        std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
        SetWindowTextW(hwnd, title.c_str());

        InvalidateRect(hwnd, NULL, FALSE);
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
            UpdateSurface(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            Render(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1; // Prevent flicker
        case WM_DROPFILES:
            OnDropFiles(hwnd, (HDROP)wParam);
            return 0;
        case WM_SIZE: {
            UpdateSurface(hwnd);
            if (!g_appState.sourceText.empty()) {
                float width = static_cast<float>(LOWORD(lParam));
                auto layout = mdviewer::LayoutEngine::ComputeLayout(g_appState.docModel, width);
                {
                    std::lock_guard<std::mutex> lock(g_appState.mtx);
                    g_appState.docLayout = std::move(layout);
                    g_appState.needsRepaint = true;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
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
