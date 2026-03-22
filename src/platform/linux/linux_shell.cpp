#include <string>
#include <cstdlib>

namespace mdviewer::linux_platform {

void OpenExternalUrl(const std::string& url) {
    std::string command = "xdg-open \"" + url + "\" &";
    std::system(command.c_str());
}

} // namespace mdviewer::linux_platform
