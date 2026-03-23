#include "platform/linux/linux_clipboard.h"

#include "GLFW/glfw3.h"

namespace mdviewer::linux_platform {

void SetClipboardText(GLFWwindow* window, const std::string& text) {
    if (window) {
        glfwSetClipboardString(window, text.c_str());
    }
}

std::string GetClipboardText(GLFWwindow* window) {
    if (window) {
        const char* text = glfwGetClipboardString(window);
        return text ? std::string(text) : "";
    }
    return "";
}

} // namespace mdviewer::linux_platform
