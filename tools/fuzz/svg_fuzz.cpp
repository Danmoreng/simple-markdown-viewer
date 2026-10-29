#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "render/mermaid_svg.h"

// Includes malformed XML, marker references, path commands and numeric values.
// No SVG resources are loaded and no renderer/network service is invoked.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;
    std::string normalized;
    std::string error;
    mdviewer::NormalizeMermaidSvg(
        std::string_view(reinterpret_cast<const char*>(data), size), normalized, error);
    return 0;
}
