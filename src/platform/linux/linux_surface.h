#pragma once

#include <cstdint>

#include "GLFW/glfw3.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"

namespace mdviewer::linux_platform {

struct LinuxSurfaceContext {
    sk_sp<GrDirectContext> skiaContext;
    sk_sp<SkSurface> surface;
    bool resizeFramePending = false;
    bool resizeLayoutPending = false;
    uint64_t nextResizeFrameTimeMs = 0;
    uint64_t nextResizeLayoutTimeMs = 0;
    uint64_t liveResizeEndTimeMs = 0;
    uint64_t nextAutoScrollTickTimeMs = 0;
};

bool InitializeSkia(GLFWwindow* window, LinuxSurfaceContext& context);
void CleanupSkia(LinuxSurfaceContext& context);
bool EnsureSurfaceSize(GLFWwindow* window, LinuxSurfaceContext& context);
void PresentSurface(GLFWwindow* window, LinuxSurfaceContext& context);

} // namespace mdviewer::linux_platform
