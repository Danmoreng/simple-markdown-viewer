#pragma once
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "util/path_utils.h"

namespace mdviewer {
struct NativeBadge {
    std::string label, message;
    uint32_t color = 0xff007ec6;
    bool square = false;
};

// Deliberately bounded static Shields subset. Unknown options/dynamic endpoints
// retain the ordinary external-image consent flow instead of inventing data.
inline std::optional<NativeBadge> ParseNativeBadge(std::string_view url) {
    constexpr std::string_view https = "https://img.shields.io/badge/";
    constexpr std::string_view http = "http://img.shields.io/badge/";
    if (url.size() > 2048) return {};
    if (url.starts_with(https)) url.remove_prefix(https.size());
    else if (url.starts_with(http)) url.remove_prefix(http.size());
    else return {};
    NativeBadge badge;
    if (const auto query = url.find('?'); query != std::string_view::npos) {
        const auto options = url.substr(query + 1);
        if (options == "style=flat-square") badge.square = true;
        else if (options != "style=flat") return {};
        url = url.substr(0, query);
    }
    if (url.find_first_of("/#") != std::string_view::npos) return {};
    auto decoded = PercentDecodePath(url);
    if (!decoded) return {};
    std::vector<std::string> fields(1);
    for (size_t i = 0; i < decoded->size(); ++i) {
        const unsigned char c = static_cast<unsigned char>((*decoded)[i]);
        if (c < 32 || c > 126) return {};
        if ((c == '-' || c == '_') && i + 1 < decoded->size() && (*decoded)[i + 1] == c) {
            fields.back() += static_cast<char>(c); ++i;
        } else if (c == '-') {
            fields.emplace_back();
            if (fields.size() > 3) return {};
        } else fields.back() += c == '_' ? ' ' : static_cast<char>(c);
    }
    if (fields.size() < 2) return {};
    if (fields.size() == 3) badge.label = fields[0];
    badge.message = fields[fields.size() - 2];
    if (badge.message.empty() || badge.label.size() > 128 || badge.message.size() > 128) return {};
    auto color = fields.back();
    std::transform(color.begin(), color.end(), color.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::pair<std::string_view, uint32_t> colors[] = {
        {"blue", 0x007ec6}, {"green", 0x97ca00}, {"brightgreen", 0x44cc11},
        {"yellow", 0xdfb317}, {"yellowgreen", 0xa4a61d}, {"orange", 0xfe7d37},
        {"red", 0xe05d44}, {"lightgrey", 0x9f9f9f}, {"gray", 0x808080},
        {"grey", 0x808080}, {"black", 0}, {"white", 0xffffff}};
    for (const auto& [name, value] : colors) {
        if (name == color) { badge.color = 0xff000000 | value; return badge; }
    }
    if (color.size() != 3 && color.size() != 6) return {};
    uint32_t value = 0;
    const auto parsed = std::from_chars(color.data(), color.data() + color.size(), value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != color.data() + color.size()) return {};
    if (color.size() == 3) value = ((value & 0xf00) << 12) | ((value & 0xf00) << 8) |
        ((value & 0xf0) << 8) | ((value & 0xf0) << 4) | ((value & 0xf) << 4) | (value & 0xf);
    badge.color = 0xff000000 | value;
    return badge;
}
} // namespace mdviewer
