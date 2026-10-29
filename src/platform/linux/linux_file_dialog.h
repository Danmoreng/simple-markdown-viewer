#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace mdviewer::linux_platform {

std::optional<std::filesystem::path> ShowOpenFileDialog();
std::optional<std::filesystem::path> ShowSavePdfDialog(const std::filesystem::path& currentFilePath);
std::optional<std::filesystem::path> ShowSaveSvgDialog(const std::filesystem::path& suggestedPath);

} // namespace mdviewer::linux_platform
