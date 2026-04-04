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

} // namespace mdviewer
