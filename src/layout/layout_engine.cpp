#include "layout_engine.h"
#include <algorithm>

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#pragma warning(pop)

namespace mdviewer {

class LayoutContext {
public:
    float currentY = 20.0f;
    float leftMargin = 40.0f;
    float rightMargin = 104.0f;
    float availableWidth;
    size_t currentTextOffset = 0;
    std::string plainText;
    SkFont font;

    LayoutContext(float width, SkTypeface* typeface)
        : availableWidth(std::max(width - leftMargin - rightMargin, 1.0f)) {
        if (typeface) {
            font.setTypeface(sk_ref_sp(typeface));
        }
    }

    void LayoutBlocks(const std::vector<Block>& blocks, std::vector<BlockLayout>& layouts, float indent = 0.0f) {
        for (const auto& block : blocks) {
            BlockLayout bl;
            bl.type = block.type;
            bl.textStart = currentTextOffset;
            
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
                LayoutRuns(block.inlineRuns, bl.lines, blockIndent, lineHeight);
                if (!block.children.empty()) {
                    LayoutBlocks(block.children, bl.children, blockIndent + 20.0f);
                }
            }

            bl.bounds = SkRect::MakeXYWH(leftMargin + blockIndent, blockTop, availableWidth - blockIndent, currentY - blockTop);
            bl.textLength = currentTextOffset - bl.textStart;
            layouts.push_back(std::move(bl));
            currentY += spacing;
        }
    }

private:
    void PushCurrentLine(std::vector<LineLayout>& lines, LineLayout& currentLine, float lineHeight) {
        lines.push_back(std::move(currentLine));
        currentY += lineHeight;
        currentLine = {currentY, lineHeight, currentTextOffset, 0, {}};
    }

    void LayoutRuns(const std::vector<InlineRun>& runs, std::vector<LineLayout>& lines, float indent, float lineHeight) {
        if (runs.empty()) return;

        LineLayout currentLine;
        currentLine.y = currentY;
        currentLine.height = lineHeight;
        currentLine.textStart = currentTextOffset;
        float currentX = 0.0f;
        float wrapWidth = std::max(availableWidth - indent, 1.0f);

        for (const auto& run : runs) {
            const char* textPtr = run.text.c_str();
            const char* endPtr = textPtr + run.text.size();

            while (textPtr < endPtr) {
                if (*textPtr == '\n') {
                    plainText.push_back('\n');
                    currentTextOffset += 1;
                    PushCurrentLine(lines, currentLine, lineHeight);
                    currentX = 0.0f;
                    ++textPtr;
                    continue;
                }

                const char* newlinePtr = std::find(textPtr, endPtr, '\n');
                const size_t remainingLength = static_cast<size_t>(newlinePtr - textPtr);
                size_t bytesConsumed = FindBreakPoint(
                    textPtr,
                    remainingLength,
                    wrapWidth - currentX,
                    currentX <= 0.0f);
                
                if (bytesConsumed == 0 && currentX > 0) {
                    PushCurrentLine(lines, currentLine, lineHeight);
                    currentX = 0.0f;
                    continue;
                }

                if (bytesConsumed == 0) {
                    bytesConsumed = remainingLength;
                }

                currentLine.runs.push_back({run.style, std::string(textPtr, bytesConsumed), currentTextOffset});
                currentLine.textLength += bytesConsumed;
                plainText.append(textPtr, bytesConsumed);
                currentTextOffset += bytesConsumed;
                
                float advance = font.measureText(textPtr, bytesConsumed, SkTextEncoding::kUTF8);
                currentX += advance;
                textPtr += bytesConsumed;

                if (currentX >= wrapWidth - 1.0f && textPtr < endPtr) {
                    PushCurrentLine(lines, currentLine, lineHeight);
                    currentX = 0.0f;
                }
            }
        }

        if (!currentLine.runs.empty()) {
            lines.push_back(std::move(currentLine));
            currentY += lineHeight;
        }
    }

    static bool IsBreakableWhitespace(char ch) {
        return ch == ' ' || ch == '\t';
    }

    size_t FindBreakPoint(const char* text, size_t length, float maxWidth, bool allowOverflowWord) {
        if (length == 0 || maxWidth <= 0.0f) {
            return 0;
        }

        size_t low = 0;
        size_t high = length;
        size_t best = 0;

        while (low <= high) {
            size_t mid = (low + high) / 2;
            while (mid > 0 && mid < length && (static_cast<unsigned char>(text[mid]) & 0xC0) == 0x80) {
                mid--;
            }
            
            if (font.measureText(text, mid, SkTextEncoding::kUTF8) <= maxWidth) {
                best = mid;
                low = mid + 1;
                while (low < length && (static_cast<unsigned char>(text[low]) & 0xC0) == 0x80) {
                    low++;
                }
            } else {
                if (mid == 0) break;
                high = mid - 1;
            }
        }

        if (best < length) {
            size_t lastSpace = 0;
            for (size_t i = 0; i < best; ++i) {
                if (IsBreakableWhitespace(text[i])) {
                    lastSpace = i + 1;
                }
            }
            if (lastSpace > 0) {
                return lastSpace;
            }
        } else {
            return best;
        }

        if (!allowOverflowWord) {
            return 0;
        }

        size_t wordEnd = 0;
        while (wordEnd < length && !IsBreakableWhitespace(text[wordEnd])) {
            ++wordEnd;
        }

        return wordEnd > 0 ? wordEnd : best;
    }
};

DocumentLayout LayoutEngine::ComputeLayout(const DocumentModel& doc, float width, SkTypeface* typeface) {
    DocumentLayout layout;
    LayoutContext ctx(width, typeface);
    ctx.LayoutBlocks(doc.blocks, layout.blocks);
    layout.totalHeight = ctx.currentY + 20.0f;
    layout.plainText = std::move(ctx.plainText);
    return layout;
}

} // namespace mdviewer
