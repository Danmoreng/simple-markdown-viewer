#include "platform/linux/linux_shell.h"

#include <spawn.h>
#include <unistd.h>

extern char** environ;

namespace mdviewer::linux_platform {

namespace {

bool SpawnDetachedOpen(const char* target) {
    if (!target || *target == '\0') {
        return false;
    }

    pid_t pid = 0;
    char* const argv[] = {
        const_cast<char*>("xdg-open"),
        const_cast<char*>(target),
        nullptr,
    };

    return posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0;
}

} // namespace

bool OpenExternalUrl(const std::string& url) {
    return SpawnDetachedOpen(url.c_str());
}

bool OpenPath(const std::filesystem::path& path) {
    return SpawnDetachedOpen(path.c_str());
}

} // namespace mdviewer::linux_platform
