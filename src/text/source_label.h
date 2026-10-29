#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "text/document_font_style.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

inline size_t DecodeSourceLabelCodepoint(std::string_view text, size_t offset, uint32_t& codepoint) {
    const auto first = static_cast<unsigned char>(text[offset]);
    const size_t count = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2 :
        (first & 0xF0) == 0xE0 ? 3 : (first & 0xF8) == 0xF0 ? 4 : 0;
    if (count == 0 || count > text.size() - offset) {
        codepoint = 0xFFFD;
        return offset + 1;
    }
    codepoint = count == 1 ? first : first & ((1U << (7U - count)) - 1U);
    for (size_t index = 1; index < count; ++index) {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xC0) != 0x80) {
            codepoint = 0xFFFD;
            return offset + 1;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    return offset + count;
}

// These labels identify an external target. Keep visual order literal and make
// invisible/directional controls visible without changing the target itself.
inline std::string EscapeSourceLabelControls(std::string_view source) {
    std::string result;
    result.reserve(source.size());
    constexpr char hex[] = "0123456789ABCDEF";
    for (size_t offset = 0; offset < source.size();) {
        uint32_t codepoint = 0;
        const size_t next = DecodeSourceLabelCodepoint(source, offset, codepoint);
        const bool escape = codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F) ||
            codepoint == 0xAD || codepoint == 0x61C ||
            (codepoint >= 0x200B && codepoint <= 0x200F) ||
            (codepoint >= 0x2028 && codepoint <= 0x202E) ||
            (codepoint >= 0x2060 && codepoint <= 0x206F) || codepoint == 0xFEFF ||
            (codepoint >= 0xE0000 && codepoint <= 0xE007F);
        if (escape) {
            result += codepoint > 0xFFFF ? "\\U" : "\\u";
            const int digits = codepoint > 0xFFFF ? 8 : 4;
            for (int index = digits - 1; index >= 0; --index) {
                result.push_back(hex[(codepoint >> (index * 4)) & 0xFU]);
            }
        } else {
            result.append(source.substr(offset, next - offset));
        }
        offset = next;
    }
    return result;
}

inline SkFont MakeSourceLabelFont(const DocumentTypefaceSet& typefaces, float size) {
    SkFont font;
    ConfigureDocumentFont(font, typefaces, BlockType::Paragraph, InlineFormatting::None, size);
    font.setSize(size);
    return font;
}

inline SkFont SourceLabelCodepointFont(
    const DocumentTypefaceSet& typefaces, const SkFont& baseFont, uint32_t codepoint) {
    SkFont font = baseFont;
    if (font.unicharToGlyph(static_cast<SkUnichar>(codepoint)) == 0 && typefaces.fontMgr) {
        const SkFontStyle style = baseFont.getTypeface()
            ? baseFont.getTypeface()->fontStyle() : SkFontStyle::Normal();
        if (auto fallback = typefaces.fontMgr->matchFamilyStyleCharacter(
                nullptr, style, nullptr, 0, static_cast<SkUnichar>(codepoint))) {
            font.setTypeface(std::move(fallback));
        }
    }
    return font;
}

// Measurement and drawing share the exact same fallback selection and advance.
// The source is a literal identifier rather than prose, so it retains source
// order instead of applying BiDi reordering to the consent label.
template <typename DrawGlyph>
inline float VisitSourceLabelText(const DocumentTypefaceSet& typefaces, const SkFont& baseFont,
    std::string_view text, DrawGlyph&& drawGlyph) {
    float width = 0.0f;
    for (size_t offset = 0; offset < text.size();) {
        uint32_t codepoint = 0;
        const size_t next = DecodeSourceLabelCodepoint(text, offset, codepoint);
        const SkFont font = SourceLabelCodepointFont(typefaces, baseFont, codepoint);
        const auto glyphText = text.substr(offset, next - offset);
        drawGlyph(glyphText, font, width);
        width += font.measureText(glyphText.data(), glyphText.size(), SkTextEncoding::kUTF8);
        offset = next;
    }
    return width;
}

inline float MeasureSourceLabelText(
    const DocumentTypefaceSet& typefaces, const SkFont& font, std::string_view text) {
    return VisitSourceLabelText(typefaces, font, text,
        [](std::string_view, const SkFont&, float) {});
}

inline float DrawSourceLabelText(SkCanvas* canvas, const DocumentTypefaceSet& typefaces,
    const SkFont& font, std::string_view text, float x, float baseline, const SkPaint& paint) {
    return VisitSourceLabelText(typefaces, font, text,
        [&](std::string_view glyphText, const SkFont& glyphFont, float advance) {
            if (canvas) canvas->drawSimpleText(glyphText.data(), glyphText.size(),
                SkTextEncoding::kUTF8, x + advance, baseline, glyphFont, paint);
        });
}

} // namespace mdviewer
