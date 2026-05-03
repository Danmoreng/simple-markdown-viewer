#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "render/theme.h"

namespace mdviewer {

enum class OutlineSide {
    Left,
    Right
};

const char* OutlineSideToString(OutlineSide side);
OutlineSide OutlineSideFromString(const char* value);

struct RecentFileConfigEntry {
    std::string pathUtf8;
    long long openedAtUnixSeconds = 0;
};

struct AppConfig {
    ThemeMode theme = ThemeMode::Light;
    OutlineSide outlineSide = OutlineSide::Left;
    std::string fontFamilyUtf8;
    float baseFontSize = 17.0f;
    std::vector<RecentFileConfigEntry> recentFiles;
};

std::optional<AppConfig> LoadAppConfig(const std::filesystem::path& path);
bool SaveAppConfig(const std::filesystem::path& path, const AppConfig& config);

} // namespace mdviewer
