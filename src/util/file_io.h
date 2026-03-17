#pragma once
#include <string>
#include <optional>
#include <filesystem>

namespace mdviewer {

std::optional<std::string> ReadFileToString(const std::filesystem::path& path);

} // namespace mdviewer
