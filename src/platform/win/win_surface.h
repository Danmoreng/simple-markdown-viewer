#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "include/core/SkSurface.h"

namespace mdviewer::win {

bool EnsureRasterSurfaceSize(HWND hwnd, sk_sp<SkSurface>& surface);
void PresentRasterSurface(HWND hwnd, SkSurface* surface);

} // namespace mdviewer::win
