#pragma once

#include <filesystem>
#include <string>

#include "app/viewer_controller.h"
#include "platform/linux/linux_surface.h"
#include "render/document_typefaces.h"
#include "render/image_cache.h"

namespace mdviewer::linux_platform {

struct LinuxHostContext {
    ViewerController& controller;
    sk_sp<SkSurface>& surface;
    DocumentTypefaceCache& typefaces;
    DocumentImageCache& imageCache;
};

class LinuxApp {
public:
    LinuxApp();

    ViewerController& Controller() { return controller_; }
    const ViewerController& Controller() const { return controller_; }

    LinuxHostContext GetHostContext() {
        return {controller_, surfaceContext_.surface, typefaces_, imageCache_};
    }

    LinuxSurfaceContext& SurfaceContext() { return surfaceContext_; }

    static std::filesystem::path GetUserConfigPath();
    static std::filesystem::path GetLegacyExecutableConfigPath();

private:
    ViewerController controller_;
    LinuxSurfaceContext surfaceContext_;
    DocumentTypefaceCache typefaces_;
    DocumentImageCache imageCache_;
};

} // namespace mdviewer::linux_platform
