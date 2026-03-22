#pragma once
#include <string>
#include <filesystem>

namespace mdviewer::linux_platform {

void OpenExternalUrl(const std::string& url);
void OpenPath(const std::filesystem::path& path);

} // namespace mdviewer::linux_platform
