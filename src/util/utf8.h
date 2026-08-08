#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace mdviewer {

struct Utf8SanitizationResult {
    std::string text;
    size_t replacementCount = 0;
};

Utf8SanitizationResult SanitizeUtf8(std::string_view input);
size_t NextUtf8Boundary(std::string_view text, size_t offset);

} // namespace mdviewer
