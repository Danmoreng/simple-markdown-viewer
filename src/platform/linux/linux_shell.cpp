#include "platform/linux/linux_shell.h"
#include <cstdlib>

namespace mdviewer::linux_platform {

void OpenExternalUrl(const std::string& url) {
    std::string command = "xdg-open \"" + url + "\" &";
    std::system(command.c_str());
}

void OpenPath(const std::filesystem::path& path) {
    std::string command = "xdg-open \"" + path.string() + "\" &";
    std::system(command.c_str());
}

} // namespace mdviewer::linux_platform
