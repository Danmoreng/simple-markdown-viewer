#include "platform/win/win_shell.h"

#include <shellapi.h>
#include <shlobj.h>

namespace mdviewer::win {

bool OpenExternalUrl(const std::string& url) {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool OpenPath(const std::filesystem::path& path) {
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool RevealInFileManager(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, error);
    const std::filesystem::path targetPath = (error ? path : absolutePath).lexically_normal();
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(targetPath.c_str());
    if (!item) {
        return false;
    }

    const HRESULT result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
    return SUCCEEDED(result);
}

} // namespace mdviewer::win
