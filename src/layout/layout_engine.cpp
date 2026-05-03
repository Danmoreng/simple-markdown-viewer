#include "layout_engine.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

#include "app/heading_anchor.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "render/typography.h"

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

namespace {

constexpr float kDocumentTopPadding = 20.0f;
constexpr float kDocumentBottomPadding = 20.0f;
constexpr float kDocumentLeftMargin = 40.0f;
constexpr float kDocumentRightMargin = 104.0f;
constexpr float kBlockSpacing = 10.0f;
constexpr float kCodeBlockOuterMarginY = 16.0f;
constexpr float kCodeBlockPaddingX = 8.0f;
constexpr float kCodeBlockPaddingY = 8.0f;
constexpr float kTableCellPaddingX = 12.0f;
constexpr float kTableCellPaddingY = 8.0f;
constexpr float kMinTableColumnWidth = 80.0f;

bool IsBreakableWhitespace(char ch) {
    return ch == ' ' || ch == '\t';
}

bool IsHeading(BlockType type) {
    return type == BlockType::Heading1 ||
           type == BlockType::Heading2 ||
           type == BlockType::Heading3 ||
           type == BlockType::Heading4 ||
           type == BlockType::Heading5 ||
           type == BlockType::Heading6;
}

} // namespace

class LayoutContext {
public:
    float currentY = kDocumentTopPadding;
    float leftMargin = kDocumentLeftMargin;
    float rightMargin = kDocumentRightMargin;
    float availableWidth;
    size_t currentTextOffset = 0;
    std::string plainText;
    std::unordered_map<std::string, float> anchors;
    SkFont font;
    float baseFontSize;
    LayoutEngine::ImageSizeProvider imageSizeProvider;

    LayoutContext(float width, SkTypeface* typeface, float documentBaseFontSize, LayoutEngine::ImageSizeProvider provider)
        : availableWidth(std::max(width - leftMargin - rightMargin, 1.0f)),
          baseFontSize(ClampBaseFontSize(documentBaseFontSize)),
          imageSizeProvider(provider) {
        if (typeface) {
            font.setTypeface(sk_ref_sp(typeface));
        }
    }

    void LayoutBlocks(const std::vector<Block>& blocks, std::vector<BlockLayout>& layouts, float indent = 0.0f) {
        for (const auto& block : blocks) {
            if (block.type == BlockType::Table) {
                LayoutTable(block, layouts, indent);
                continue;
            }

            BlockLayout bl;
            bl.type = block.type;
            bl.align = block.align;
            bl.taskListState = block.taskListState;
            bl.orderedListStart = block.orderedListStart;
            bl.orderedListDelimiter = block.orderedListDelimiter;
            bl.codeLanguage = block.codeLanguage;
            bl.textStart = currentTextOffset;

            float fontSize = GetBlockFontSize(block.type, baseFontSize);

            font.setSize(fontSize);
            SkFontMetrics metrics;
            font.getMetrics(&metrics);
            float lineHeight = metrics.fDescent - metrics.fAscent + metrics.fLeading;

            float blockIndent = indent + (block.type == BlockType::ListItem ? 20.0f : 0.0f);
            const float blockLeft = leftMargin + blockIndent;
            const float blockWidth = std::max(availableWidth - blockIndent, 1.0f);

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockOuterMarginY;
            }

            float blockTop = currentY;
            float contentLeft = blockLeft;
            float contentWidth = blockWidth;

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockPaddingY;
                contentLeft += kCodeBlockPaddingX;
                contentWidth = std::max(contentWidth - (kCodeBlockPaddingX * 2.0f), 1.0f);
            }

            if (IsHeading(block.type)) {
                AddHeadingAnchor(block, blockTop);
            }

            if (block.type == BlockType::ThematicBreak) {
                currentY += 20.0f;
            } else {
                const float inlineTop = currentY;
                const std::vector<InlineRun> highlightedCodeRuns =
                    block.type == BlockType::CodeBlock
                        ? syntax::HighlightCodeBlock(block.codeLanguage, block.inlineRuns)
                        : std::vector<InlineRun>{};
                const std::vector<InlineRun>& layoutRuns =
                    block.type == BlockType::CodeBlock ? highlightedCodeRuns : block.inlineRuns;
                const float inlineHeight = LayoutRuns(
                    layoutRuns,
                    bl.lines,
                    inlineTop,
                    contentLeft,
                    contentWidth,
                    lineHeight,
                    block.type,
                    block.align);
                currentY = inlineTop + inlineHeight;

                if (!block.children.empty()) {
                    LayoutBlocks(block.children, bl.children, blockIndent + 20.0f);
                }
            }

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockPaddingY;
            }

            bl.bounds = SkRect::MakeXYWH(blockLeft, blockTop, blockWidth, currentY - blockTop);

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockOuterMarginY;
            }

            bl.textLength = currentTextOffset - bl.textStart;
            layouts.push_back(std::move(bl));
            currentY += kBlockSpacing;
        }
    }

private:
    static float ResolveLineX(float contentLeft, float wrapWidth, float lineWidth, TextAlign align) {
        switch (align) {
            case TextAlign::Center:
                return contentLeft + std::max((wrapWidth - lineWidth) * 0.5f, 0.0f);
            case TextAlign::Right:
                return contentLeft + std::max(wrapWidth - lineWidth, 0.0f);
            case TextAlign::Default:
            case TextAlign::Left:
            default:
                return contentLeft;
        }
    }

    void PushCurrentLine(
        std::vector<LineLayout>& lines,
        LineLayout& currentLine,
        float& lineY,
        float baseLineHeight,
        float contentLeft,
        float wrapWidth,
        float lineWidth,
        TextAlign align) {
        currentLine.x = ResolveLineX(contentLeft, wrapWidth, lineWidth, align);
        lines.push_back(std::move(currentLine));
        lineY += lines.back().height;
        currentLine = {};
        currentLine.x = contentLeft;
        currentLine.y = lineY;
        currentLine.height = baseLineHeight;
        currentLine.textStart = currentTextOffset;
    }

    float LayoutRuns(
        const std::vector<InlineRun>& runs,
        std::vector<LineLayout>& lines,
        float startY,
        float contentLeft,
        float wrapWidth,
        float lineHeight,
        BlockType blockType,
        TextAlign align) {
        LineLayout currentLine;
        currentLine.x = contentLeft;
        currentLine.y = startY;
        currentLine.height = lineHeight;
        currentLine.textStart = currentTextOffset;
        float lineY = startY;
        float currentX = 0.0f;
        float currentLineWidth = 0.0f;
        wrapWidth = std::max(wrapWidth, 1.0f);

        const bool isSingleImageBlock = (runs.size() == 1 && runs[0].style == InlineStyle::Image);

        for (const auto& run : runs) {
            ConfigureInlineFont(blockType, run.style);
            if (run.style == InlineStyle::Image) {
                float imgDisplayW, imgDisplayH;
                float actualW = 0.0f, actualH = 0.0f;
                bool hasActualSize = false;
                if (imageSizeProvider) {
                    auto size = imageSizeProvider(run.url);
                    if (size.first > 0 && size.second > 0) {
                        actualW = size.first;
                        actualH = size.second;
                        hasActualSize = true;
                    }
                }

                if (isSingleImageBlock) {
                    const float maxBlockImageWidth = wrapWidth * 0.9f;
                    imgDisplayW = hasActualSize
                        ? std::min(maxBlockImageWidth, actualW)
                        : maxBlockImageWidth;
                    imgDisplayW = std::max(imgDisplayW, 1.0f);
                    float aspect = hasActualSize ? (actualH / actualW) : 0.618f;
                    imgDisplayH = imgDisplayW * aspect;
                } else {
                    imgDisplayH = lineHeight * 0.8f;
                    float aspect = hasActualSize ? (actualW / actualH) : 1.5f;
                    imgDisplayW = imgDisplayH * aspect;
                }

                if (currentX + imgDisplayW > wrapWidth && currentX > 0.0f) {
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }

                RunLayout rl = {run.style, run.text, run.url, currentTextOffset, imgDisplayW, imgDisplayH};
                currentLine.runs.push_back(std::move(rl));
                currentLine.height = std::max(currentLine.height, imgDisplayH + 4.0f);
                currentLineWidth = currentX + imgDisplayW;
                currentX = currentLineWidth + 4.0f;
                continue;
            }

            const char* textPtr = run.text.c_str();
            const char* endPtr = textPtr + run.text.size();

            while (textPtr < endPtr) {
                if (*textPtr == '\n') {
                    plainText.push_back('\n');
                    currentTextOffset += 1;
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
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
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                    continue;
                }

                if (bytesConsumed == 0) {
                    bytesConsumed = remainingLength;
                }

                currentLine.runs.push_back({run.style, std::string(textPtr, bytesConsumed), run.url, currentTextOffset});
                currentLine.textLength += bytesConsumed;
                plainText.append(textPtr, bytesConsumed);
                currentTextOffset += bytesConsumed;
                
                float advance = font.measureText(textPtr, bytesConsumed, SkTextEncoding::kUTF8);
                currentLineWidth = currentX + advance;
                currentX = currentLineWidth;
                textPtr += bytesConsumed;

                if (currentX >= wrapWidth - 1.0f && textPtr < endPtr) {
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }
            }
        }

        if (!currentLine.runs.empty()) {
            currentLine.x = ResolveLineX(contentLeft, wrapWidth, currentLineWidth, align);
            lines.push_back(std::move(currentLine));
            lineY += lines.back().height;
        }
        return lineY - startY;
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

    void ConfigureInlineFont(BlockType blockType, InlineStyle inlineStyle) {
        const BlockType fontBlockType = inlineStyle == InlineStyle::Code ? BlockType::CodeBlock : blockType;
        font.setSize(GetBlockFontSize(fontBlockType, baseFontSize));
    }

    void AddHeadingAnchor(const Block& block, float y) {
        std::string text;
        for (const auto& run : block.inlineRuns) {
            if (run.style != InlineStyle::Image) {
                text += run.text;
            }
        }

        const std::string slug = MakeHeadingAnchor(text);
        if (slug.empty()) {
            return;
        }

        std::string uniqueSlug = slug;
        int suffix = 1;
        while (anchors.contains(uniqueSlug)) {
            ++suffix;
            uniqueSlug = slug + "-" + std::to_string(suffix);
        }
        anchors[uniqueSlug] = y;
    }

    float GetLineHeight(BlockType blockType, InlineStyle inlineStyle = InlineStyle::Plain) {
        ConfigureInlineFont(blockType, inlineStyle);
        SkFontMetrics metrics;
        font.getMetrics(&metrics);
        return metrics.fDescent - metrics.fAscent + metrics.fLeading;
    }

    const Block* GetRowCell(const Block& row, size_t columnIndex) const {
        size_t cellIndex = 0;
        for (const auto& child : row.children) {
            if (child.type != BlockType::TableHeaderCell && child.type != BlockType::TableCell) {
                continue;
            }
            if (cellIndex == columnIndex) {
                return &child;
            }
            ++cellIndex;
        }
        return nullptr;
    }

    float MeasureInlineRunsWidth(const std::vector<InlineRun>& runs, BlockType blockType) {
        float width = 0.0f;
        for (const auto& run : runs) {
            if (run.style == InlineStyle::Image) {
                if (imageSizeProvider) {
                    const auto size = imageSizeProvider(run.url);
                    width += std::max(size.first, 0.0f);
                }
                continue;
            }

            ConfigureInlineFont(blockType, run.style);
            width += font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
        }
        return width;
    }

    std::vector<float> ComputeTableColumnWidths(
        const std::vector<const Block*>& rows,
        size_t columnCount,
        float tableWidth) {
        std::vector<float> preferredWidths(columnCount, kMinTableColumnWidth);
        for (const Block* row : rows) {
            for (size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                const Block* cell = GetRowCell(*row, columnIndex);
                if (cell == nullptr) {
                    continue;
                }
                const float contentWidth = MeasureInlineRunsWidth(cell->inlineRuns, cell->type) + (kTableCellPaddingX * 2.0f);
                preferredWidths[columnIndex] = std::max(preferredWidths[columnIndex], contentWidth);
            }
        }

        const float preferredTotal = std::max(
            std::accumulate(preferredWidths.begin(), preferredWidths.end(), 0.0f),
            1.0f);
        std::vector<float> widths(columnCount, tableWidth / static_cast<float>(columnCount));
        if (preferredTotal <= tableWidth) {
            widths = preferredWidths;
            const float extra = (tableWidth - preferredTotal) / static_cast<float>(columnCount);
            for (auto& width : widths) {
                width += extra;
            }
        } else {
            const float scale = tableWidth / preferredTotal;
            for (size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                widths[columnIndex] = std::max(preferredWidths[columnIndex] * scale, kMinTableColumnWidth);
            }

            const float scaledTotal = std::accumulate(widths.begin(), widths.end(), 0.0f);
            if (scaledTotal > tableWidth) {
                const float shrink = tableWidth / scaledTotal;
                for (auto& width : widths) {
                    width *= shrink;
                }
            }
        }

        return widths;
    }

    void AppendPlainTextSeparator(char ch) {
        plainText.push_back(ch);
        currentTextOffset += 1;
    }

    void LayoutTable(const Block& block, std::vector<BlockLayout>& layouts, float indent) {
        std::vector<const Block*> rows;
        rows.reserve(block.children.size());
        for (const auto& child : block.children) {
            if (child.type == BlockType::TableRow) {
                rows.push_back(&child);
            }
        }

        BlockLayout tableLayout;
        tableLayout.type = BlockType::Table;
        tableLayout.align = block.align;
        tableLayout.textStart = currentTextOffset;

        const float tableLeft = leftMargin + indent;
        const float tableWidth = std::max(availableWidth - indent, 1.0f);
        const float tableTop = currentY;

        if (rows.empty()) {
            tableLayout.bounds = SkRect::MakeXYWH(tableLeft, tableTop, tableWidth, 0.0f);
            tableLayout.textLength = 0;
            layouts.push_back(std::move(tableLayout));
            currentY += kBlockSpacing;
            return;
        }

        size_t columnCount = 0;
        for (const Block* row : rows) {
            size_t rowColumnCount = 0;
            for (const auto& child : row->children) {
                if (child.type == BlockType::TableHeaderCell || child.type == BlockType::TableCell) {
                    ++rowColumnCount;
                }
            }
            columnCount = std::max(columnCount, rowColumnCount);
        }
        columnCount = std::max<size_t>(columnCount, 1);

        const std::vector<float> columnWidths = ComputeTableColumnWidths(rows, columnCount, tableWidth);
        float rowTop = currentY;

        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const Block& row = *rows[rowIndex];
            BlockLayout rowLayout;
            rowLayout.type = BlockType::TableRow;
            rowLayout.textStart = currentTextOffset;

            float rowHeight = 0.0f;
            for (size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
                const Block* cell = GetRowCell(row, columnIndex);
                const bool isHeaderCell = cell != nullptr && cell->type == BlockType::TableHeaderCell;
                const BlockType cellType = isHeaderCell ? BlockType::TableHeaderCell : BlockType::TableCell;
                const TextAlign cellAlign = cell != nullptr ? cell->align : TextAlign::Default;
                float cellLeft = tableLeft;
                for (size_t previousColumn = 0; previousColumn < columnIndex; ++previousColumn) {
                    cellLeft += columnWidths[previousColumn];
                }
                const float cellWidth =
                    (columnIndex + 1 == columnCount)
                        ? (tableLeft + tableWidth - cellLeft)
                        : columnWidths[columnIndex];
                const float cellInnerLeft = cellLeft + kTableCellPaddingX;
                const float cellInnerTop = rowTop + kTableCellPaddingY;
                const float cellInnerWidth = std::max(cellWidth - (kTableCellPaddingX * 2.0f), 1.0f);

                BlockLayout cellLayout;
                cellLayout.type = cellType;
                cellLayout.align = cellAlign;
                cellLayout.textStart = currentTextOffset;

                const float lineHeight = GetLineHeight(cellType, isHeaderCell ? InlineStyle::Strong : InlineStyle::Plain);
                float contentHeight = 0.0f;
                if (cell != nullptr) {
                    contentHeight = LayoutRuns(
                        cell->inlineRuns,
                        cellLayout.lines,
                        cellInnerTop,
                        cellInnerLeft,
                        cellInnerWidth,
                        lineHeight,
                        cellType,
                        cellAlign);
                }

                const float cellHeight = std::max(contentHeight, lineHeight) + (kTableCellPaddingY * 2.0f);
                cellLayout.bounds = SkRect::MakeXYWH(cellLeft, rowTop, cellWidth, cellHeight);
                cellLayout.textLength = currentTextOffset - cellLayout.textStart;
                rowHeight = std::max(rowHeight, cellHeight);
                rowLayout.children.push_back(std::move(cellLayout));

                if (columnIndex + 1 < columnCount) {
                    AppendPlainTextSeparator('\t');
                }
            }

            for (auto& cellLayout : rowLayout.children) {
                cellLayout.bounds = SkRect::MakeXYWH(
                    cellLayout.bounds.left(),
                    cellLayout.bounds.top(),
                    cellLayout.bounds.width(),
                    rowHeight);
            }

            rowLayout.bounds = SkRect::MakeXYWH(tableLeft, rowTop, tableWidth, rowHeight);
            rowLayout.textLength = currentTextOffset - rowLayout.textStart;
            tableLayout.children.push_back(std::move(rowLayout));

            rowTop += rowHeight;
            if (rowIndex + 1 < rows.size()) {
                AppendPlainTextSeparator('\n');
            }
        }

        currentY = rowTop;
        tableLayout.bounds = SkRect::MakeXYWH(tableLeft, tableTop, tableWidth, currentY - tableTop);
        tableLayout.textLength = currentTextOffset - tableLayout.textStart;
        layouts.push_back(std::move(tableLayout));
        currentY += kBlockSpacing;
    }
};

DocumentLayout LayoutEngine::ComputeLayout(
    const DocumentModel& doc,
    float width,
    SkTypeface* typeface,
    float baseFontSize,
    ImageSizeProvider imageSizeProvider) {
    DocumentLayout layout;
    LayoutContext ctx(width, typeface, baseFontSize, imageSizeProvider);
    ctx.LayoutBlocks(doc.blocks, layout.blocks);
    layout.totalHeight = ctx.currentY + kDocumentBottomPadding;
    layout.plainText = std::move(ctx.plainText);
    layout.anchors = std::move(ctx.anchors);
    return layout;
}

} // namespace mdviewer
