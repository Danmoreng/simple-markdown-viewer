#include "platform/linux/linux_app.h"

#include <unistd.h>
#include <limits.h>

namespace mdviewer::linux_platform {

LinuxApp::LinuxApp() = default;

std::filesystem::path LinuxApp::GetExecutableConfigPath() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        std::filesystem::path exePath(std::string(result, count));
        return exePath.parent_path() / "mdviewer.ini";
    }
    return "mdviewer.ini";
}

} // namespace mdviewer::linux_platform
