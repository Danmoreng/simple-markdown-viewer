#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mdviewer {

// Shared strings are UTF-8, including on Windows where narrow paths are not.
inline std::filesystem::path PathFromUtf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
inline std::string PathToUtf8(const std::filesystem::path& value) {
    const auto utf8 = value.u8string();
    return std::string(utf8.begin(), utf8.end());
}
inline std::optional<std::string> PercentDecodePath(std::string_view value) {
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c == '%') {
            if (i + 2 >= value.size()) return std::nullopt;
            const int high = hex(value[i + 1]);
            const int low = hex(value[i + 2]);
            if (high < 0 || low < 0) return std::nullopt;
            c = static_cast<char>((high << 4) | low);
            i += 2;
        }
        if (c == '\0') return std::nullopt;
        // '+' is literal in paths/fragments, not form-encoded whitespace.
        result.push_back(c);
    }
    return result;
}
inline std::filesystem::path NormalizeDocumentPath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}
} // namespace mdviewer
