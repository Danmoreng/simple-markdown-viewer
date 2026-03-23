#pragma once

#include <string>

struct GLFWwindow;

namespace mdviewer::linux_platform {

void SetClipboardText(GLFWwindow* window, const std::string& text);
std::string GetClipboardText(GLFWwindow* window);

} // namespace mdviewer::linux_platform
