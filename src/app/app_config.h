#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "render/theme.h"

namespace mdviewer {

struct AppConfig {
    ThemeMode theme = ThemeMode::Light;
    std::string fontFamilyUtf8;
    float baseFontSize = 17.0f;
};

std::optional<AppConfig> LoadAppConfig(const std::filesystem::path& path);
bool SaveAppConfig(const std::filesystem::path& path, const AppConfig& config);

} // namespace mdviewer
