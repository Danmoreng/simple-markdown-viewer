#pragma once
#include <string>
#include <filesystem>

namespace mdviewer::linux_platform {

bool OpenExternalUrl(const std::string& url);
bool OpenPath(const std::filesystem::path& path);

} // namespace mdviewer::linux_platform
