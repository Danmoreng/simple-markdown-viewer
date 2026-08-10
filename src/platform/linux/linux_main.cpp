#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <exception>
#include <filesystem>

#include "GLFW/glfw3.h"
#include "platform/linux/linux_app.h"
#include "platform/linux/linux_interaction.h"
#include "platform/linux/linux_surface.h"
#include "platform/linux/linux_viewer_host.h"
#include "platform/linux/linux_window_icon.h"
#include "view/document_interaction.h"

namespace mdviewer::linux_platform {

namespace {

constexpr int kInitialWindowWidth = 900;
constexpr int kInitialWindowHeight = 1200;
constexpr uint64_t kResizeFrameIntervalMs = 17; // Approximately 60 FPS.
constexpr uint64_t kResizeLayoutIntervalMs = 34; // Approximately 30 FPS.
constexpr uint64_t kAutoScrollTickIntervalMs = 16;
constexpr float kAutoScrollDeadZone = 2.0f;

uint64_t GetCurrentTickCountMs() {
    return static_cast<uint64_t>(glfwGetTime() * 1000.0);
}

WindowPlacement ClampWindowPlacementToDisplays(WindowPlacement placement) {
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (!monitors || monitorCount == 0) {
        return placement;
    }

    GLFWmonitor* bestMonitor = monitors[0];
    long long bestIntersectionArea = -1;
    for (int index = 0; index < monitorCount; ++index) {
        int monitorX = 0;
        int monitorY = 0;
        int monitorWidth = 0;
        int monitorHeight = 0;
        glfwGetMonitorWorkarea(monitors[index], &monitorX, &monitorY, &monitorWidth, &monitorHeight);

        const long long windowRight = static_cast<long long>(placement.x) + placement.width;
        const long long windowBottom = static_cast<long long>(placement.y) + placement.height;
        const long long monitorRight = static_cast<long long>(monitorX) + monitorWidth;
        const long long monitorBottom = static_cast<long long>(monitorY) + monitorHeight;
        const long long intersectionWidth = std::max(
            0LL,
            std::min(windowRight, monitorRight) -
                std::max(static_cast<long long>(placement.x), static_cast<long long>(monitorX)));
        const long long intersectionHeight = std::max(
            0LL,
            std::min(windowBottom, monitorBottom) -
                std::max(static_cast<long long>(placement.y), static_cast<long long>(monitorY)));
        const long long intersectionArea = intersectionWidth * intersectionHeight;
        if (intersectionArea > bestIntersectionArea) {
            bestIntersectionArea = intersectionArea;
            bestMonitor = monitors[index];
        }
    }

    int workX = 0;
    int workY = 0;
    int workWidth = placement.width;
    int workHeight = placement.height;
    glfwGetMonitorWorkarea(bestMonitor, &workX, &workY, &workWidth, &workHeight);

    placement.width = std::min(placement.width, workWidth);
    placement.height = std::min(placement.height, workHeight);
    placement.x = std::clamp(placement.x, workX, workX + workWidth - placement.width);
    placement.y = std::clamp(placement.y, workY, workY + workHeight - placement.height);
    return placement;
}

void SaveWindowPlacement(GLFWwindow* window, ViewerController& controller) {
    WindowPlacement placement;
    glfwGetWindowPos(window, &placement.x, &placement.y);
    glfwGetWindowSize(window, &placement.width, &placement.height);
    controller.SetWindowPlacement(placement);
    controller.SaveConfig();
}

} // namespace

int RunLinuxAppImpl(int argc, char* argv[]) {
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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "mdviewer");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "mdviewer");

    LinuxApp app;
    app.Controller().SetConfigPath(LinuxApp::GetUserConfigPath());
    app.Controller().SetLegacyConfigPath(LinuxApp::GetLegacyExecutableConfigPath());
    app.Controller().LoadConfig();

    std::cerr << "Creating window..." << std::endl;
    const auto savedPlacement = app.Controller().GetWindowPlacement();
    const WindowPlacement placement = savedPlacement
        ? ClampWindowPlacementToDisplays(*savedPlacement)
        : WindowPlacement{0, 0, kInitialWindowWidth, kInitialWindowHeight};
    GLFWwindow* window = glfwCreateWindow(
        placement.width,
        placement.height,
        "Markdown Viewer",
        nullptr,
        nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return 1;
    }
    if (savedPlacement) {
        glfwSetWindowPos(window, placement.x, placement.y);
    }
    if (!SetLinuxWindowIcon(window)) {
        std::cerr << "Warning: Linux window icon could not be loaded." << std::endl;
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

    glfwShowWindow(window);
    std::cerr << "Entering main loop..." << std::endl;
    GetAppState(app.GetHostContext()).needsRepaint = true;
    while (!glfwWindowShouldClose(window)) {
        try {
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
            auto& surfaceContext = app.SurfaceContext();
            if (appState.isAutoScrolling) {
                if (surfaceContext.nextAutoScrollTickTimeMs == 0 ||
                    surfaceContext.nextAutoScrollTickTimeMs <= nowMs) {
                    if (TickAutoScroll(appState, GetMaxScroll(window, app.GetHostContext()), kAutoScrollDeadZone)) {
                        appState.needsRepaint = true;
                    }
                    surfaceContext.nextAutoScrollTickTimeMs = nowMs + kAutoScrollTickIntervalMs;
                }
            } else {
                surfaceContext.nextAutoScrollTickTimeMs = 0;
            }
            if (surfaceContext.resizeFramePending &&
                surfaceContext.nextResizeFrameTimeMs <= nowMs) {
                surfaceContext.resizeFramePending = false;
                surfaceContext.nextResizeFrameTimeMs = nowMs + kResizeFrameIntervalMs;
                if (EnsureSurfaceSize(window, surfaceContext)) {
                    // Present the existing layout first. This is much cheaper than
                    // relayout and keeps the client-drawn menu bar anchored to the
                    // newest window surface at display-frame cadence.
                    Render(window, app.GetHostContext());
                    PresentSurface(window, surfaceContext);
                    appState.needsRepaint = false;
                }
            }
            if (surfaceContext.resizeLayoutPending &&
                surfaceContext.nextResizeLayoutTimeMs <= nowMs) {
                surfaceContext.resizeLayoutPending = false;
                surfaceContext.nextResizeLayoutTimeMs = nowMs + kResizeLayoutIntervalMs;
                if (EnsureSurfaceSize(window, surfaceContext)) {
                    RelayoutCurrentDocument(window, app.GetHostContext());
                    appState.needsRepaint = true;
                }
            }
            if (surfaceContext.liveResizeEndTimeMs > 0 &&
                surfaceContext.liveResizeEndTimeMs <= nowMs) {
                surfaceContext.liveResizeEndTimeMs = 0;
                app.GetHostContext().imageCache.EndLiveResize();
                appState.needsRepaint = true;
            }

            if (appState.needsRepaint) {
                appState.needsRepaint = false;
                if (EnsureSurfaceSize(window, app.SurfaceContext())) {
                    Render(window, app.GetHostContext());
                    PresentSurface(window, app.SurfaceContext());
                }
            }

            const uint64_t waitNowMs = GetCurrentTickCountMs();
            const uint64_t nextFeedbackTimeout = std::min({
                appState.copiedFeedbackTimeout > waitNowMs ? appState.copiedFeedbackTimeout : UINT64_MAX,
                appState.zoomFeedbackTimeout > waitNowMs ? appState.zoomFeedbackTimeout : UINT64_MAX,
                surfaceContext.liveResizeEndTimeMs > waitNowMs
                    ? surfaceContext.liveResizeEndTimeMs
                    : UINT64_MAX,
                surfaceContext.resizeFramePending
                    ? std::max(surfaceContext.nextResizeFrameTimeMs, waitNowMs)
                    : UINT64_MAX,
                surfaceContext.resizeLayoutPending
                    ? std::max(surfaceContext.nextResizeLayoutTimeMs, waitNowMs)
                    : UINT64_MAX,
                appState.isAutoScrolling
                    ? std::max(surfaceContext.nextAutoScrollTickTimeMs, waitNowMs)
                    : UINT64_MAX,
            });
            if (nextFeedbackTimeout != UINT64_MAX) {
                const double timeoutSeconds = static_cast<double>(nextFeedbackTimeout - waitNowMs) / 1000.0;
                glfwWaitEventsTimeout(timeoutSeconds);
            } else {
                glfwWaitEvents();
            }
        } catch (const std::exception& error) {
            std::cerr << "Linux main loop failed during update/render: " << error.what() << std::endl;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        } catch (...) {
            std::cerr << "Linux main loop failed during update/render: unknown exception" << std::endl;
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    std::cerr << "Cleaning up..." << std::endl;
    SaveWindowPlacement(window, app.Controller());
    CleanupInteractionResources();
    CleanupSkia(app.SurfaceContext());
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int RunLinuxApp(int argc, char* argv[]) {
    try {
        return RunLinuxAppImpl(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Linux application failed during startup/shutdown: " << error.what() << std::endl;
    } catch (...) {
        std::cerr << "Linux application failed during startup/shutdown: unknown exception" << std::endl;
    }
    glfwTerminate();
    return 1;
}

} // namespace mdviewer::linux_platform
