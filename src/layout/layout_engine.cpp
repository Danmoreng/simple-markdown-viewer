#include "layout_engine.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#pragma warning(pop)

namespace mdviewer {

DocumentLayout LayoutEngine::ComputeLayout(const DocumentModel& doc, float width) {
    DocumentLayout layout;
    float currentY = 20.0f;
    float horizontalMargin = 40.0f;
    float availableWidth = width - (horizontalMargin * 2.0f);

    if (availableWidth <= 0) availableWidth = 100.0f;

    SkFont font;

    for (const auto& block : doc.blocks) {
        BlockLayout bl;
        bl.type = block.type;
        
        float fontSize = 16.0f;
        float spacing = 10.0f;

        if (block.type == BlockType::Heading1) fontSize = 32.0f;
        else if (block.type == BlockType::Heading2) fontSize = 24.0f;
        else if (block.type == BlockType::Heading3) fontSize = 20.0f;

        font.setSize(fontSize);
        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        float lineHeight = metrics.fDescent - metrics.fAscent + metrics.fLeading;

        float blockTop = currentY;

        if (block.type != BlockType::ThematicBreak) {
            std::string fullText;
            for (const auto& run : block.inlineRuns) {
                fullText += run.text;
            }

            const char* textPtr = fullText.c_str();
            const char* endPtr = textPtr + fullText.size();

            while (textPtr < endPtr) {
                // Manual breakText using measureText (simplified)
                size_t bytesConsumed = 0;
                size_t low = 0;
                size_t high = endPtr - textPtr;
                
                // Binary search for approximate break point
                while (low <= high) {
                    size_t mid = (low + high) / 2;
                    // Ensure we don't break in middle of UTF-8 char
                    while (mid > 0 && (textPtr[mid] & 0xC0) == 0x80) mid--;
                    
                    float measuredWidth = font.measureText(textPtr, mid, SkTextEncoding::kUTF8);
                    if (measuredWidth <= availableWidth) {
                        bytesConsumed = mid;
                        low = mid + 1;
                        // Skip forward to next UTF-8 start
                        while (low < high && (textPtr[low] & 0xC0) == 0x80) low++;
                    } else {
                        if (mid == 0) { bytesConsumed = 1; break; }
                        high = mid - 1;
                    }
                }

                if (bytesConsumed == 0) bytesConsumed = 1;

                // Try to break at whitespace
                if (textPtr + bytesConsumed < endPtr) {
                    const char* lastSpace = textPtr;
                    for (const char* p = textPtr; p < textPtr + bytesConsumed; ++p) {
                        if (*p == ' ') lastSpace = p;
                    }
                    if (lastSpace > textPtr) {
                        bytesConsumed = lastSpace - textPtr + 1;
                    }
                }

                LineLayout line;
                line.y = currentY;
                line.height = lineHeight;
                line.runs.push_back({InlineStyle::Plain, std::string(textPtr, bytesConsumed)});
                
                bl.lines.push_back(line);
                currentY += lineHeight;
                textPtr += bytesConsumed;
            }
        } else {
            currentY += 20.0f;
        }

        bl.bounds = SkRect::MakeXYWH(horizontalMargin, blockTop, availableWidth, currentY - blockTop);
        layout.blocks.push_back(std::move(bl));
        currentY += spacing;
    }

    layout.totalHeight = currentY + 20.0f;
    return layout;
}

} // namespace mdviewer
