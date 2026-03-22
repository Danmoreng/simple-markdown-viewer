#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

#include "GLFW/glfw3.h"
#include "platform/linux/linux_app.h"
#include "platform/linux/linux_interaction.h"
#include "platform/linux/linux_surface.h"
#include "platform/linux/linux_viewer_host.h"

namespace mdviewer::linux_platform {

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
    app.Controller().SetConfigPath(LinuxApp::GetExecutableConfigPath());
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
        if (GetAppState(app.GetHostContext()).needsRepaint) {
            GetAppState(app.GetHostContext()).needsRepaint = false;
            if (EnsureSurfaceSize(window, app.SurfaceContext())) {
                Render(window, app.GetHostContext());
                PresentSurface(window, app.SurfaceContext());
            }
        }

        glfwWaitEvents();
    }

    std::cerr << "Cleaning up..." << std::endl;
    CleanupSkia(app.SurfaceContext());
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace mdviewer::linux_platform
