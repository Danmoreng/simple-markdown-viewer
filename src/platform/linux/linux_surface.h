#pragma once

#include "GLFW/glfw3.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"

namespace mdviewer::linux_platform {

struct LinuxSurfaceContext {
    GrDirectContext* skiaContext = nullptr;
    sk_sp<SkSurface> surface;
};

bool InitializeSkia(GLFWwindow* window, LinuxSurfaceContext& context);
void CleanupSkia(LinuxSurfaceContext& context);
bool EnsureSurfaceSize(GLFWwindow* window, LinuxSurfaceContext& context);
void PresentSurface(GLFWwindow* window, LinuxSurfaceContext& context);

} // namespace mdviewer::linux_platform
