#include "app/app_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "render/typography.h"

namespace mdviewer {

namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

} // namespace

std::optional<AppConfig> LoadAppConfig(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    AppConfig config;
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
        } else if (key == "font_family") {
            config.fontFamilyUtf8 = value;
        } else if (key == "base_font_size") {
            try {
                config.baseFontSize = ClampBaseFontSize(std::stof(value));
            } catch (...) {
                config.baseFontSize = kDefaultBaseFontSize;
            }
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
    output << "font_family=" << config.fontFamilyUtf8 << '\n';
    output << "base_font_size=" << ClampBaseFontSize(config.baseFontSize) << '\n';
    return output.good();
}

} // namespace mdviewer
