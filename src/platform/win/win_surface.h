#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>

#include "include/core/SkSurface.h"

namespace mdviewer::win {

class WinSurface {
public:
    WinSurface();
    ~WinSurface();

    WinSurface(const WinSurface&) = delete;
    WinSurface& operator=(const WinSurface&) = delete;

    bool EnsureSize(HWND hwnd);
    bool BeginFrame(HWND hwnd, bool interactiveResize = false);
    void Present(HWND hwnd, bool interactiveResize = false);
    void Shutdown();

    [[nodiscard]] SkSurface* get() const;
    [[nodiscard]] SkSurface* operator->() const;
    [[nodiscard]] explicit operator bool() const;
    [[nodiscard]] bool IsGpuBacked() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mdviewer::win
