#include "platform/linux/linux_app.h"

#include <cstdlib>
#include <unistd.h>
#include <limits.h>

namespace mdviewer::linux_platform {

LinuxApp::LinuxApp() = default;

std::filesystem::path LinuxApp::GetUserConfigPath() {
    if (const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        xdgConfigHome != nullptr && xdgConfigHome[0] != '\0') {
        return std::filesystem::path(xdgConfigHome) / "simple-markdown-viewer" / "mdviewer.ini";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".config" / "simple-markdown-viewer" / "mdviewer.ini";
    }

    return std::filesystem::path("mdviewer.ini");
}

std::filesystem::path LinuxApp::GetLegacyExecutableConfigPath() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        std::filesystem::path exePath(std::string(result, count));
        return exePath.parent_path() / "mdviewer.ini";
    }
    return "mdviewer.ini";
}

} // namespace mdviewer::linux_platform
