#include "text/document_font_style.h"

#include "render/typography.h"

#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkRefCnt.h"

namespace mdviewer {

bool IsHeadingBlock(BlockType blockType) {
    return blockType == BlockType::Heading1 ||
           blockType == BlockType::Heading2 ||
           blockType == BlockType::Heading3 ||
           blockType == BlockType::Heading4 ||
           blockType == BlockType::Heading5 ||
           blockType == BlockType::Heading6;
}

void ConfigureDocumentFont(
    SkFont& font,
    const DocumentTypefaceSet& typefaces,
    BlockType blockType,
    InlineFormatting formatting,
    float baseFontSize) {
    const bool isCode = blockType == BlockType::CodeBlock ||
        HasFormatting(formatting, InlineFormatting::Code) ||
        HasFormatting(formatting, InlineFormatting::Keyboard);
    const bool isHeading = IsHeadingBlock(blockType);
    const bool isStrong = HasFormatting(formatting, InlineFormatting::Strong) ||
        blockType == BlockType::TableHeaderCell;
    font.setTypeface(sk_ref_sp(
        isCode ? typefaces.code : (isHeading ? typefaces.heading : (isStrong ? typefaces.bold : typefaces.regular))));
    font.setSize(
        isCode
            ? GetBlockFontSize(BlockType::CodeBlock, baseFontSize)
            : GetBlockFontSize(blockType, baseFontSize));
    if (HasFormatting(formatting, InlineFormatting::Keyboard)) {
        font.setSize(font.getSize() * 0.9f);
    }
    if (HasFormatting(formatting, InlineFormatting::Subscript) ||
        HasFormatting(formatting, InlineFormatting::Superscript)) {
        font.setSize(font.getSize() * 0.78f);
    }
    font.setSubpixel(!isHeading);
    font.setHinting(SkFontHinting::kSlight);
    font.setEdging(isHeading ? SkFont::Edging::kAntiAlias : SkFont::Edging::kSubpixelAntiAlias);
    font.setEmbolden(isStrong && (isCode || isHeading));
    font.setSkewX(HasFormatting(formatting, InlineFormatting::Emphasis) ? -0.18f : 0.0f);
    font.setScaleX(1.0f);
}

}  // namespace mdviewer
