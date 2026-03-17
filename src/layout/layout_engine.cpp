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

class LayoutContext {
public:
    float currentY = 20.0f;
    float horizontalMargin = 40.0f;
    float availableWidth;
    SkFont font;

    LayoutContext(float width) : availableWidth(width - (horizontalMargin * 2.0f)) {}

    void LayoutBlocks(const std::vector<Block>& blocks, std::vector<BlockLayout>& layouts, float indent = 0.0f) {
        for (const auto& block : blocks) {
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
            float blockIndent = indent + (block.type == BlockType::ListItem ? 20.0f : 0.0f);

            if (block.type == BlockType::ThematicBreak) {
                currentY += 20.0f;
            } else {
                // Layout inline runs with wrapping
                LayoutRuns(block.inlineRuns, bl.lines, blockIndent, lineHeight);
                
                // Recursively layout children (for nested lists, etc.)
                if (!block.children.empty()) {
                    LayoutBlocks(block.children, bl.children, blockIndent + 20.0f);
                }
            }

            bl.bounds = SkRect::MakeXYWH(horizontalMargin + blockIndent, blockTop, availableWidth - blockIndent, currentY - blockTop);
            layouts.push_back(std::move(bl));
            currentY += spacing;
        }
    }

private:
    void LayoutRuns(const std::vector<InlineRun>& runs, std::vector<LineLayout>& lines, float indent, float lineHeight) {
        if (runs.empty()) return;

        LineLayout currentLine;
        currentLine.y = currentY;
        currentLine.height = lineHeight;
        float currentX = 0.0f;
        float wrapWidth = availableWidth - indent;

        for (const auto& run : runs) {
            const char* textPtr = run.text.c_str();
            const char* endPtr = textPtr + run.text.size();

            while (textPtr < endPtr) {
                size_t bytesConsumed = FindBreakPoint(textPtr, endPtr - textPtr, wrapWidth - currentX);
                
                if (bytesConsumed == 0 && currentX > 0) {
                    // Could not fit even one char, and we're not at start of line. Wrap.
                    lines.push_back(std::move(currentLine));
                    currentY += lineHeight;
                    currentLine = {currentY, lineHeight, {}};
                    currentX = 0.0f;
                    continue;
                }

                if (bytesConsumed == 0) bytesConsumed = 1; // Force at least one char

                currentLine.runs.push_back({run.style, std::string(textPtr, bytesConsumed)});
                
                float advance = font.measureText(textPtr, bytesConsumed, SkTextEncoding::kUTF8);
                currentX += advance;
                textPtr += bytesConsumed;

                if (currentX >= wrapWidth - 1.0f && textPtr < endPtr) {
                    lines.push_back(std::move(currentLine));
                    currentY += lineHeight;
                    currentLine = {currentY, lineHeight, {}};
                    currentX = 0.0f;
                }
            }
        }

        if (!currentLine.runs.empty()) {
            lines.push_back(std::move(currentLine));
            currentY += lineHeight;
        }
    }

    size_t FindBreakPoint(const char* text, size_t length, float maxWidth) {
        size_t low = 0;
        size_t high = length;
        size_t best = 0;

        while (low <= high) {
            size_t mid = (low + high) / 2;
            while (mid > 0 && (text[mid] & 0xC0) == 0x80) mid--;
            
            if (font.measureText(text, mid, SkTextEncoding::kUTF8) <= maxWidth) {
                best = mid;
                low = mid + 1;
                while (low < length && (text[low] & 0xC0) == 0x80) low++;
            } else {
                if (mid == 0) break;
                high = mid - 1;
            }
        }

        // Try to snap to whitespace
        if (best < length) {
            size_t lastSpace = 0;
            for (size_t i = 0; i < best; ++i) {
                if (text[i] == ' ') lastSpace = i + 1;
            }
            if (lastSpace > 0) return lastSpace;
        }

        return best;
    }
};

DocumentLayout LayoutEngine::ComputeLayout(const DocumentModel& doc, float width) {
    DocumentLayout layout;
    LayoutContext ctx(width);
    ctx.LayoutBlocks(doc.blocks, layout.blocks);
    layout.totalHeight = ctx.currentY + 20.0f;
    return layout;
}

} // namespace mdviewer
