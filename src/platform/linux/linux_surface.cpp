#include "platform/linux/linux_surface.h"

#include <iostream>

#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/core/SkColorSpace.h"

namespace mdviewer::linux_platform {

bool InitializeSkia(GLFWwindow* window, LinuxSurfaceContext& context) {
    glfwMakeContextCurrent(window);

    auto interface = GrGLMakeNativeInterface();
    if (!interface) {
        std::cerr << "Failed to create Skia GL interface" << std::endl;
        return false;
    }

    context.skiaContext = GrDirectContexts::MakeGL(interface).release();
    if (!context.skiaContext) {
        std::cerr << "Failed to create Skia GPU context" << std::endl;
        return false;
    }

    return true;
}

void CleanupSkia(LinuxSurfaceContext& context) {
    context.surface.reset();
    if (context.skiaContext) {
        context.skiaContext->abandonContext();
        delete context.skiaContext;
        context.skiaContext = nullptr;
    }
}

bool EnsureSurfaceSize(GLFWwindow* window, LinuxSurfaceContext& context) {
    if (!context.skiaContext) return false;

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    if (width <= 0 || height <= 0) {
        context.surface.reset();
        return false;
    }

    if (context.surface && context.surface->width() == width && context.surface->height() == height) {
        return true;
    }

    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = 0; // Use default FBO
    fbInfo.fFormat = 0x8058; // GL_RGBA8

    GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeGL(
        width,
        height,
        0, // sample count
        8, // stencil bits
        fbInfo);

    SkSurfaceProps props;
    context.surface = SkSurfaces::WrapBackendRenderTarget(
        context.skiaContext,
        backendRT,
        kBottomLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType,
        nullptr,
        &props);

    if (!context.surface) {
        std::cerr << "Failed to create Skia surface" << std::endl;
        return false;
    }

    return true;
}

void PresentSurface(GLFWwindow* window, LinuxSurfaceContext& context) {
    if (context.skiaContext) {
        context.skiaContext->flushAndSubmit();
    }
    glfwSwapBuffers(window);
}

} // namespace mdviewer::linux_platform
