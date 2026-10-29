#include <cstddef>
#include <cstdint>
#include <string>

#include "markdown/markdown_parser.h"

// Parse arbitrary bytes, including invalid UTF-8 and nested Markdown/HTML.
// This exercises the same md4c callbacks and model builder as document loading.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;
    const auto document = mdviewer::MarkdownParser::Parse(
        std::string(reinterpret_cast<const char*>(data), size));
    (void)document;
    return 0;
}
