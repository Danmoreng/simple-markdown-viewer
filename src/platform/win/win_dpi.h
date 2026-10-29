#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/win/win_coordinate_helpers.h"

namespace mdviewer::win {

inline float WindowDpiScale(HWND hwnd) { return DpiScale(GetDpiForWindow(hwnd)); }

inline float LogicalClientWidth(HWND hwnd) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    return PixelsToLogical(static_cast<float>(bounds.right - bounds.left), WindowDpiScale(hwnd));
}

inline float LogicalClientHeight(HWND hwnd) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    return PixelsToLogical(static_cast<float>(bounds.bottom - bounds.top), WindowDpiScale(hwnd));
}

inline POINT LogicalClientPoint(HWND hwnd, POINT pixelPoint) {
    const float scale = WindowDpiScale(hwnd);
    return {LogicalCoordinate(pixelPoint.x, scale), LogicalCoordinate(pixelPoint.y, scale)};
}

} // namespace mdviewer::win
