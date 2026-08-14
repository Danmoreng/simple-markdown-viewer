#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>

#include "app/viewer_controller.h"
#include "platform/win/win_file_watcher.h"
#include "platform/win/win_interaction.h"
#include "platform/win/win_surface.h"
#include "platform/win/win_viewer_host.h"
#include "render/document_typefaces.h"
#include "render/image_cache.h"

namespace mdviewer::win {

class WinApp {
public:
    WinApp();

    ViewerController& Controller() { return controller_; }
    const ViewerController& Controller() const { return controller_; }

    ViewerHostContext& Host() { return hostContext_; }
    ViewerInteractionContext& Interaction() { return interactionContext_; }

    std::wstring GetSelectedFontFamily() const;
    static std::string WideToUtf8(const std::wstring& text);
    static std::wstring Utf8ToWide(const std::string& text);
    static std::filesystem::path GetUserConfigPath();
    static std::filesystem::path GetLegacyExecutableConfigPath();

private:
    ViewerController controller_;
    WinSurface surface_;
    DocumentTypefaceCache typefaces_;
    DocumentImageCache imageCache_;
    WinFileWatcher fileWatcher_;
    ViewerHostContext hostContext_;
    ViewerInteractionContext interactionContext_;
};

} // namespace mdviewer::win
