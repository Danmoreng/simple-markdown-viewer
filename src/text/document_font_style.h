#pragma once

#include "layout/document_model.h"
#include "text/document_fonts.h"

class SkFont;

namespace mdviewer {

bool IsHeadingBlock(BlockType blockType);

void ConfigureDocumentFont(
    SkFont& font,
    const DocumentTypefaceSet& typefaces,
    BlockType blockType,
    InlineFormatting formatting,
    float baseFontSize);

}  // namespace mdviewer
