#include "app/heading_anchor.h"

#include <cctype>
#include <cstdint>

namespace mdviewer {

namespace {

size_t DecodeUtf8Codepoint(std::string_view text, size_t offset, uint32_t& codepoint) {
    const auto byte0 = static_cast<unsigned char>(text[offset]);
    if (byte0 < 0x80) {
        codepoint = byte0;
        return offset + 1;
    }

    auto continuation = [&](size_t index) -> uint32_t {
        if (index >= text.size()) {
            return 0;
        }
        const auto byte = static_cast<unsigned char>(text[index]);
        return (byte & 0xC0) == 0x80 ? static_cast<uint32_t>(byte & 0x3F) : 0;
    };

    if ((byte0 & 0xE0) == 0xC0 && offset + 1 < text.size()) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x1F) << 6) | continuation(offset + 1);
        return offset + 2;
    }
    if ((byte0 & 0xF0) == 0xE0 && offset + 2 < text.size()) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x0F) << 12) |
                    (continuation(offset + 1) << 6) |
                    continuation(offset + 2);
        return offset + 3;
    }
    if ((byte0 & 0xF8) == 0xF0 && offset + 3 < text.size()) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x07) << 18) |
                    (continuation(offset + 1) << 12) |
                    (continuation(offset + 2) << 6) |
                    continuation(offset + 3);
        return offset + 4;
    }

    codepoint = byte0;
    return offset + 1;
}

bool IsUtf8ContinuationAt(std::string_view text, size_t offset) {
    return offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80;
}

bool IsUnicodeWhitespace(uint32_t codepoint) {
    return codepoint == 0x00A0 ||
           codepoint == 0x1680 ||
           (codepoint >= 0x2000 && codepoint <= 0x200A) ||
           codepoint == 0x2028 ||
           codepoint == 0x2029 ||
           codepoint == 0x202F ||
           codepoint == 0x205F ||
           codepoint == 0x3000;
}

bool IsCombiningMark(uint32_t codepoint) {
    return (codepoint >= 0x0300 && codepoint <= 0x036F) ||
           (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||
           (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||
           (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||
           (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

bool IsEmojiOrSymbol(uint32_t codepoint) {
    return (codepoint >= 0x1F000 && codepoint <= 0x1FAFF) ||
           (codepoint >= 0x2600 && codepoint <= 0x27BF) ||
           codepoint == 0xFE0F;
}

} // namespace

std::string MakeHeadingAnchor(std::string_view text) {
    std::string slug;
    bool lastWasHyphen = false;
    for (size_t offset = 0; offset < text.size();) {
        uint32_t codepoint = 0;
        const size_t nextOffset = DecodeUtf8Codepoint(text, offset, codepoint);
        if (codepoint < 0x80) {
            const auto ch = static_cast<unsigned char>(codepoint);
            if (std::isalnum(ch)) {
                slug.push_back(static_cast<char>(std::tolower(ch)));
                lastWasHyphen = false;
            } else if (std::isspace(ch) || ch == '-') {
                if (!slug.empty() && !lastWasHyphen) {
                    slug.push_back('-');
                    lastWasHyphen = true;
                }
            }
        } else if (IsUnicodeWhitespace(codepoint)) {
            if (!slug.empty() && !lastWasHyphen) {
                slug.push_back('-');
                lastWasHyphen = true;
            }
        } else if (!IsCombiningMark(codepoint) && !IsEmojiOrSymbol(codepoint)) {
            slug.append(text.substr(offset, nextOffset - offset));
            lastWasHyphen = false;
        }

        offset = nextOffset;
        while (IsUtf8ContinuationAt(text, offset)) {
            ++offset;
        }
    }

    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

} // namespace mdviewer
