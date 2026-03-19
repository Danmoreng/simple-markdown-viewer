#include "platform/win/win_shell.h"

#include <shellapi.h>

namespace mdviewer::win {

bool OpenExternalUrl(const std::string& url) {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool OpenPath(const std::filesystem::path& path) {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

} // namespace mdviewer::win
