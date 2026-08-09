#include "layout_engine.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <utility>

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
constexpr float kCompactDocumentMargin = 12.0f;
constexpr float kCompactViewportWidth = 480.0f;
constexpr float kFullMarginViewportWidth = 900.0f;
constexpr float kBlockSpacing = 10.0f;
constexpr float kCodeBlockOuterMarginY = 16.0f;
constexpr float kCodeBlockPaddingX = 8.0f;
constexpr float kCodeBlockPaddingY = 8.0f;
constexpr float kTableCellPaddingX = 12.0f;
constexpr float kTableCellPaddingY = 8.0f;
constexpr float kMinTableColumnWidth = 80.0f;
constexpr float kMaxTableColumnWidth = 420.0f;
constexpr float kHorizontalScrollbarSpace = 12.0f;

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

int HeadingLevel(BlockType type) {
    switch (type) {
        case BlockType::Heading1: return 1;
        case BlockType::Heading2: return 2;
        case BlockType::Heading3: return 3;
        case BlockType::Heading4: return 4;
        case BlockType::Heading5: return 5;
        case BlockType::Heading6: return 6;
        default: return 1;
    }
}

} // namespace

DocumentHorizontalInsets GetDocumentHorizontalInsets(float viewportWidth) {
    const float interpolation = std::clamp(
        (viewportWidth - kCompactViewportWidth) /
            (kFullMarginViewportWidth - kCompactViewportWidth),
        0.0f,
        1.0f);
    return {
        kCompactDocumentMargin + ((kDocumentLeftMargin - kCompactDocumentMargin) * interpolation),
        kCompactDocumentMargin + ((kDocumentRightMargin - kCompactDocumentMargin) * interpolation),
    };
}

class LayoutContext {
public:
    float currentY = kDocumentTopPadding;
    float leftMargin;
    float rightMargin;
    float availableWidth;
    size_t currentTextOffset = 0;
    std::string plainText;
    std::unordered_map<std::string, float> anchors;
    std::vector<HeadingOutlineItem> outline;
    SkFont font;
    float baseFontSize;
    LayoutEngine::ImageSizeProvider imageSizeProvider;

    LayoutContext(float width, SkTypeface* typeface, float documentBaseFontSize, LayoutEngine::ImageSizeProvider provider)
        : leftMargin(GetDocumentHorizontalInsets(width).left),
          rightMargin(GetDocumentHorizontalInsets(width).right),
          availableWidth(std::max(width - leftMargin - rightMargin, 1.0f)),
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
            font.setEmbolden(false);
            font.setSkewX(0.0f);
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
                std::vector<InlineRun> highlightedCodeRuns;
                if (block.type == BlockType::CodeBlock) {
                    syntax::HighlightResult highlightResult =
                        syntax::HighlightCodeBlock(block.codeLanguage, block.inlineRuns);
                    highlightedCodeRuns = std::move(highlightResult.runs);
                    bl.codeHighlightStatus = highlightResult.status;
                }
                const std::vector<InlineRun>& layoutRuns = block.type == BlockType::CodeBlock
                    ? highlightedCodeRuns
                    : block.inlineRuns;
                float laidOutContentWidth = 0.0f;
                const float inlineHeight = LayoutRuns(
                    layoutRuns,
                    bl.lines,
                    inlineTop,
                    contentLeft,
                    contentWidth,
                    lineHeight,
                    block.type,
                    block.align,
                    block.type == BlockType::CodeBlock,
                    &laidOutContentWidth);
                currentY = inlineTop + inlineHeight;
                if (block.type == BlockType::CodeBlock) {
                    bl.codeContentWidth = laidOutContentWidth;
                    bl.codeViewportWidth = contentWidth;
                    bl.horizontalContentWidth = laidOutContentWidth;
                    bl.horizontalViewportWidth = contentWidth;
                    bl.usesHorizontalScrollOffset = laidOutContentWidth > contentWidth + 0.5f;
                    bl.horizontalScrollOwnerTextStart = bl.textStart;
                }

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
        TextAlign align,
        bool preserveSourceLines = false,
        float* laidOutContentWidth = nullptr) {
        LineLayout currentLine;
        currentLine.x = contentLeft;
        currentLine.y = startY;
        currentLine.height = lineHeight;
        currentLine.textStart = currentTextOffset;
        float lineY = startY;
        float currentX = 0.0f;
        float currentLineWidth = 0.0f;
        float maxContentWidth = 0.0f;
        wrapWidth = std::max(wrapWidth, 1.0f);

        const bool isSingleImageBlock = (runs.size() == 1 && runs[0].kind == InlineKind::Image);

        for (const auto& run : runs) {
            ConfigureInlineFont(blockType, run.formatting);
            if (run.kind == InlineKind::HardBreak) {
                plainText.push_back('\n');
                currentTextOffset += 1;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                currentX = 0.0f;
                currentLineWidth = 0.0f;
                continue;
            }
            if (run.kind == InlineKind::Image) {
                float imgDisplayW, imgDisplayH;
                float actualW = 0.0f, actualH = 0.0f;
                bool hasActualSize = false;
                if (imageSizeProvider) {
                    auto size = imageSizeProvider(run.imageSource);
                    if (size.first > 0 && size.second > 0) {
                        actualW = size.first;
                        actualH = size.second;
                        hasActualSize = true;
                    }
                }

                const bool hasRequestedWidth = run.imageRequestedWidth > 0.0f;
                const bool hasRequestedHeight = run.imageRequestedHeight > 0.0f;
                if (hasRequestedWidth || hasRequestedHeight) {
                    const float aspect = hasActualSize ? (actualW / actualH) : 1.618f;
                    imgDisplayW = hasRequestedWidth
                        ? run.imageRequestedWidth
                        : run.imageRequestedHeight * aspect;
                    imgDisplayH = hasRequestedHeight
                        ? run.imageRequestedHeight
                        : run.imageRequestedWidth / aspect;
                    const float fitScale = std::min(1.0f, wrapWidth / std::max(imgDisplayW, 1.0f));
                    imgDisplayW = std::max(imgDisplayW * fitScale, 1.0f);
                    imgDisplayH = std::max(imgDisplayH * fitScale, 1.0f);
                } else if (isSingleImageBlock) {
                    const float maxBlockImageWidth = wrapWidth * 0.9f;
                    imgDisplayW = hasActualSize
                        ? std::min(maxBlockImageWidth, actualW)
                        : maxBlockImageWidth;
                    imgDisplayW = std::max(imgDisplayW, 1.0f);
                    float aspect = hasActualSize ? (actualH / actualW) : 0.618f;
                    imgDisplayH = imgDisplayW * aspect;
                } else if (hasActualSize) {
                    imgDisplayH = lineHeight * 0.8f;
                    const float aspect = actualW / actualH;
                    imgDisplayW = imgDisplayH * aspect;
                } else {
                    ConfigureInlineFont(blockType, run.formatting);
                    imgDisplayH = lineHeight * 1.05f;
                    const float labelWidth = font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                    imgDisplayW = std::clamp(labelWidth + 14.0f, 28.0f, wrapWidth);
                }

                if (currentX + imgDisplayW > wrapWidth && currentX > 0.0f) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }

                RunLayout rl{
                    .formatting = run.formatting,
                    .kind = InlineKind::Image,
                    .syntaxRole = run.syntaxRole,
                    .text = run.text,
                    .imageSource = run.imageSource,
                    .textStart = currentTextOffset,
                    .imageWidth = imgDisplayW,
                    .imageHeight = imgDisplayH,
                    .linkTarget = run.linkTarget,
                };
                currentLine.runs.push_back(std::move(rl));
                currentLine.height = std::max(currentLine.height, imgDisplayH + 4.0f);
                currentLineWidth = currentX + imgDisplayW;
                currentX = currentLineWidth + 4.0f;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                continue;
            }

            const char softBreakText[] = " ";
            const char* textPtr = run.kind == InlineKind::SoftBreak ? softBreakText : run.text.c_str();
            const char* endPtr = textPtr + (run.kind == InlineKind::SoftBreak ? 1 : run.text.size());

            while (textPtr < endPtr) {
                if (*textPtr == '\n') {
                    plainText.push_back('\n');
                    currentTextOffset += 1;
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                    ++textPtr;
                    continue;
                }

                const char* newlinePtr = std::find(textPtr, endPtr, '\n');
                const size_t remainingLength = static_cast<size_t>(newlinePtr - textPtr);
                size_t bytesConsumed = preserveSourceLines
                    ? remainingLength
                    : FindBreakPoint(
                          textPtr,
                          remainingLength,
                          wrapWidth - currentX,
                          currentX <= 0.0f);
                
                if (bytesConsumed == 0 && currentX > 0) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                    continue;
                }

                if (bytesConsumed == 0) {
                    bytesConsumed = remainingLength;
                }

                RunLayout rl{
                    .formatting = run.formatting,
                    .kind = InlineKind::Text,
                    .syntaxRole = run.syntaxRole,
                    .text = std::string(textPtr, bytesConsumed),
                    .textStart = currentTextOffset,
                    .linkTarget = run.linkTarget,
                };
                currentLine.runs.push_back(std::move(rl));
                currentLine.textLength += bytesConsumed;
                plainText.append(textPtr, bytesConsumed);
                currentTextOffset += bytesConsumed;
                
                float advance = font.measureText(textPtr, bytesConsumed, SkTextEncoding::kUTF8);
                currentLineWidth = currentX + advance;
                currentX = currentLineWidth;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                textPtr += bytesConsumed;

                if (!preserveSourceLines && currentX >= wrapWidth - 1.0f && textPtr < endPtr) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }
            }
        }

        if (!currentLine.runs.empty()) {
            maxContentWidth = std::max(maxContentWidth, currentLineWidth);
            currentLine.x = ResolveLineX(contentLeft, wrapWidth, currentLineWidth, align);
            lines.push_back(std::move(currentLine));
            lineY += lines.back().height;
        }
        if (laidOutContentWidth != nullptr) {
            *laidOutContentWidth = maxContentWidth;
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
        if (best > 0) {
            return best;
        }
        size_t firstBoundary = 1;
        while (firstBoundary < length &&
               (static_cast<unsigned char>(text[firstBoundary]) & 0xC0) == 0x80) {
            ++firstBoundary;
        }
        return firstBoundary;
    }

    void ConfigureInlineFont(BlockType blockType, InlineFormatting formatting) {
        const BlockType fontBlockType = HasFormatting(formatting, InlineFormatting::Code)
            ? BlockType::CodeBlock
            : blockType;
        font.setSize(GetBlockFontSize(fontBlockType, baseFontSize));
        font.setEmbolden(HasFormatting(formatting, InlineFormatting::Strong));
        font.setSkewX(HasFormatting(formatting, InlineFormatting::Emphasis) ? -0.18f : 0.0f);
    }

    void AddHeadingAnchor(const Block& block, float y) {
        std::string text;
        for (const auto& run : block.inlineRuns) {
            if (run.kind == InlineKind::Text) {
                text += run.text;
            } else if (run.kind == InlineKind::SoftBreak || run.kind == InlineKind::HardBreak) {
                text += ' ';
            }
        }

        const std::string slug = MakeHeadingAnchor(text);
        if (slug.empty()) {
            outline.push_back(HeadingOutlineItem{
                .level = HeadingLevel(block.type),
                .text = text,
                .slug = {},
                .y = y,
            });
            return;
        }

        std::string uniqueSlug = slug;
        int suffix = 1;
        while (anchors.contains(uniqueSlug)) {
            ++suffix;
            uniqueSlug = slug + "-" + std::to_string(suffix);
        }
        anchors[uniqueSlug] = y;
        outline.push_back(HeadingOutlineItem{
            .level = HeadingLevel(block.type),
            .text = std::move(text),
            .slug = uniqueSlug,
            .y = y,
        });
    }

    float GetLineHeight(BlockType blockType, InlineFormatting formatting = InlineFormatting::None) {
        ConfigureInlineFont(blockType, formatting);
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
        float maxWidth = 0.0f;
        for (const auto& run : runs) {
            if (run.kind == InlineKind::HardBreak) {
                maxWidth = std::max(maxWidth, width);
                width = 0.0f;
                continue;
            }
            if (run.kind == InlineKind::Image) {
                if (imageSizeProvider) {
                    const auto size = imageSizeProvider(run.imageSource);
                    width += std::max(size.first, 0.0f);
                }
                continue;
            }

            ConfigureInlineFont(blockType, run.formatting);
            if (run.kind == InlineKind::SoftBreak) {
                width += font.measureText(" ", 1, SkTextEncoding::kUTF8);
            } else {
                width += font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
            }
        }
        return std::max(maxWidth, width);
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
                preferredWidths[columnIndex] = std::max(
                    preferredWidths[columnIndex],
                    std::min(contentWidth, kMaxTableColumnWidth));
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
            widths = preferredWidths;
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
        const float tableContentWidth = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0f);
        const bool horizontallyScrollable = tableContentWidth > tableWidth + 0.5f;
        tableLayout.horizontalContentWidth = tableContentWidth;
        tableLayout.horizontalViewportWidth = tableWidth;
        float rowTop = currentY;

        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const Block& row = *rows[rowIndex];
            BlockLayout rowLayout;
            rowLayout.type = BlockType::TableRow;
            rowLayout.textStart = currentTextOffset;
            rowLayout.usesHorizontalScrollOffset = horizontallyScrollable;
            rowLayout.horizontalScrollOwnerTextStart = tableLayout.textStart;

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
                        ? (tableLeft + tableContentWidth - cellLeft)
                        : columnWidths[columnIndex];
                const float cellInnerLeft = cellLeft + kTableCellPaddingX;
                const float cellInnerTop = rowTop + kTableCellPaddingY;
                const float cellInnerWidth = std::max(cellWidth - (kTableCellPaddingX * 2.0f), 1.0f);

                BlockLayout cellLayout;
                cellLayout.type = cellType;
                cellLayout.align = cellAlign;
                cellLayout.textStart = currentTextOffset;
                cellLayout.usesHorizontalScrollOffset = horizontallyScrollable;
                cellLayout.horizontalScrollOwnerTextStart = tableLayout.textStart;

                const float lineHeight = GetLineHeight(
                    cellType,
                    isHeaderCell ? InlineFormatting::Strong : InlineFormatting::None);
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

            rowLayout.bounds = SkRect::MakeXYWH(tableLeft, rowTop, tableContentWidth, rowHeight);
            rowLayout.textLength = currentTextOffset - rowLayout.textStart;
            tableLayout.children.push_back(std::move(rowLayout));

            rowTop += rowHeight;
            if (rowIndex + 1 < rows.size()) {
                AppendPlainTextSeparator('\n');
            }
        }

        currentY = rowTop + (horizontallyScrollable ? kHorizontalScrollbarSpace : 0.0f);
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
    layout.outline = std::move(ctx.outline);
    return layout;
}

} // namespace mdviewer
