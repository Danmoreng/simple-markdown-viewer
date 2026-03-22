#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace mdviewer::linux_platform {

std::optional<std::filesystem::path> ShowOpenFileDialog();

} // namespace mdviewer::linux_platform
