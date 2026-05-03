#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <filesystem>

#include "GLFW/glfw3.h"
#include "platform/linux/linux_app.h"
#include "platform/linux/linux_interaction.h"
#include "platform/linux/linux_surface.h"
#include "platform/linux/linux_viewer_host.h"

namespace mdviewer::linux_platform {

namespace {

uint64_t GetCurrentTickCountMs() {
    return static_cast<uint64_t>(glfwGetTime() * 1000.0);
}

} // namespace

int RunLinuxApp(int argc, char* argv[]) {
    std::cerr << "Starting Linux application..." << std::endl;
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    LinuxApp app;
    app.Controller().SetConfigPath(LinuxApp::GetUserConfigPath());
    app.Controller().SetLegacyConfigPath(LinuxApp::GetLegacyExecutableConfigPath());
    app.Controller().LoadConfig();

    std::cerr << "Creating window..." << std::endl;
    GLFWwindow* window = glfwCreateWindow(900, 1200, "Markdown Viewer", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }

    std::cerr << "Initializing Skia..." << std::endl;
    if (!InitializeSkia(window, app.SurfaceContext())) {
        std::cerr << "Failed to initialize Skia" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!EnsureFontSystem(app.GetHostContext())) {
        std::cerr << "Failed to initialize font system" << std::endl;
        CleanupSkia(app.SurfaceContext());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    SetupCallbacks(window, &app);

    if (argc > 1) {
        std::cerr << "Loading initial file: " << argv[1] << std::endl;
        LoadFile(window, app.GetHostContext(), argv[1]);
    }

    std::cerr << "Entering main loop..." << std::endl;
    GetAppState(app.GetHostContext()).needsRepaint = true;
    while (!glfwWindowShouldClose(window)) {
        auto& appState = GetAppState(app.GetHostContext());
        const uint64_t nowMs = GetCurrentTickCountMs();
        if (appState.copiedFeedbackTimeout > 0 && appState.copiedFeedbackTimeout <= nowMs) {
            appState.copiedFeedbackTimeout = 0;
            appState.needsRepaint = true;
        }
        if (appState.zoomFeedbackTimeout > 0 && appState.zoomFeedbackTimeout <= nowMs) {
            appState.zoomFeedbackTimeout = 0;
            appState.needsRepaint = true;
        }

        if (appState.needsRepaint) {
            appState.needsRepaint = false;
            if (EnsureSurfaceSize(window, app.SurfaceContext())) {
                Render(window, app.GetHostContext());
                PresentSurface(window, app.SurfaceContext());
            }
        }

        const uint64_t nextFeedbackTimeout = std::min(
            appState.copiedFeedbackTimeout > nowMs ? appState.copiedFeedbackTimeout : UINT64_MAX,
            appState.zoomFeedbackTimeout > nowMs ? appState.zoomFeedbackTimeout : UINT64_MAX);
        if (nextFeedbackTimeout != UINT64_MAX) {
            const double timeoutSeconds = static_cast<double>(nextFeedbackTimeout - nowMs) / 1000.0;
            glfwWaitEventsTimeout(timeoutSeconds);
        } else {
            glfwWaitEvents();
        }
    }

    std::cerr << "Cleaning up..." << std::endl;
    CleanupSkia(app.SurfaceContext());
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace mdviewer::linux_platform
