#pragma once

#include "GLFW/glfw3.h"
#include "platform/linux/linux_app.h"

namespace mdviewer::linux_platform {

void SetupCallbacks(GLFWwindow* window, LinuxApp* app);

} // namespace mdviewer::linux_platform
