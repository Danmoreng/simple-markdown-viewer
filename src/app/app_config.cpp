#include "app/app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>

#include "render/typography.h"

namespace mdviewer {

namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::optional<size_t> ParseRecentFileIndex(const std::string& key) {
    constexpr std::string_view prefix = "recent_file_";
    if (key.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const std::string suffix = key.substr(prefix.size());
    if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }

    try {
        return static_cast<size_t>(std::stoull(suffix));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<size_t> ParseRecentOpenedAtIndex(const std::string& key) {
    constexpr std::string_view prefix = "recent_file_";
    constexpr std::string_view suffix = "_opened_at";
    if (key.rfind(prefix, 0) != 0 || key.size() <= prefix.size() + suffix.size()) {
        return std::nullopt;
    }
    if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return std::nullopt;
    }

    const std::string indexText = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
    if (indexText.empty() || indexText.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }

    try {
        return static_cast<size_t>(std::stoull(indexText));
    } catch (...) {
        return std::nullopt;
    }
}

long long ParseUnixSeconds(const std::string& value) {
    try {
        return std::max(0LL, std::stoll(value));
    } catch (...) {
        return 0;
    }
}

} // namespace

const char* OutlineSideToString(OutlineSide side) {
    switch (side) {
        case OutlineSide::Right:
            return "right";
        case OutlineSide::Left:
        default:
            return "left";
    }
}

OutlineSide OutlineSideFromString(const char* value) {
    if (!value) {
        return OutlineSide::Left;
    }

    std::string normalized = Trim(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "right") {
        return OutlineSide::Right;
    }
    return OutlineSide::Left;
}

std::optional<AppConfig> LoadAppConfig(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    AppConfig config;
    std::map<size_t, RecentFileConfigEntry> recentFilesByIndex;
    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = Trim(trimmed.substr(1, trimmed.size() - 2));
            continue;
        }

        const size_t equalsPos = trimmed.find('=');
        if (equalsPos == std::string::npos || section != "app") {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, equalsPos));
        const std::string value = Trim(trimmed.substr(equalsPos + 1));

        if (key == "theme") {
            config.theme = ThemeModeFromString(value.c_str());
        } else if (key == "outline_side") {
            config.outlineSide = OutlineSideFromString(value.c_str());
        } else if (key == "font_family") {
            config.fontFamilyUtf8 = value;
        } else if (key == "base_font_size") {
            try {
                config.baseFontSize = ClampBaseFontSize(std::stof(value));
            } catch (...) {
                config.baseFontSize = kDefaultBaseFontSize;
            }
        } else if (const auto fileIndex = ParseRecentFileIndex(key); fileIndex && !value.empty()) {
            recentFilesByIndex[*fileIndex].pathUtf8 = value;
        } else if (const auto openedAtIndex = ParseRecentOpenedAtIndex(key); openedAtIndex) {
            recentFilesByIndex[*openedAtIndex].openedAtUnixSeconds = ParseUnixSeconds(value);
        }
    }

    for (auto& [recentIndex, recentFile] : recentFilesByIndex) {
        (void)recentIndex;
        if (!recentFile.pathUtf8.empty()) {
            config.recentFiles.push_back(std::move(recentFile));
        }
    }

    return config;
}

bool SaveAppConfig(const std::filesystem::path& path, const AppConfig& config) {
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "[app]\n";
    output << "theme=" << ThemeModeToString(config.theme) << '\n';
    output << "outline_side=" << OutlineSideToString(config.outlineSide) << '\n';
    output << "font_family=" << config.fontFamilyUtf8 << '\n';
    output << "base_font_size=" << ClampBaseFontSize(config.baseFontSize) << '\n';
    for (size_t index = 0; index < config.recentFiles.size(); ++index) {
        output << "recent_file_" << index << '=' << config.recentFiles[index].pathUtf8 << '\n';
        if (config.recentFiles[index].openedAtUnixSeconds > 0) {
            output << "recent_file_" << index << "_opened_at=" << config.recentFiles[index].openedAtUnixSeconds << '\n';
        }
    }
    return output.good();
}

} // namespace mdviewer
