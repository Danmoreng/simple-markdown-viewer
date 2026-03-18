#include "platform/win/win_surface.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#pragma warning(pop)

namespace mdviewer::win {

bool EnsureRasterSurfaceSize(HWND hwnd, sk_sp<SkSurface>& surface) {
    RECT rect = {};
    GetClientRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        surface.reset();
        return false;
    }

    if (surface && surface->width() == width && surface->height() == height) {
        return true;
    }

    const SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    surface = SkSurfaces::Raster(info);
    return surface != nullptr;
}

void PresentRasterSurface(HWND hwnd, SkSurface* surface) {
    PAINTSTRUCT ps;
    if (!surface) {
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    SkPixmap pixmap;
    if (!surface->peekPixels(&pixmap)) {
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    HDC hdc = BeginPaint(hwnd, &ps);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pixmap.width();
    bmi.bmiHeader.biHeight = -pixmap.height();
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        0,
        0,
        pixmap.width(),
        pixmap.height(),
        0,
        0,
        pixmap.width(),
        pixmap.height(),
        pixmap.addr(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);

    EndPaint(hwnd, &ps);
}

} // namespace mdviewer::win
