#include "layout_engine.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <numeric>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <utf8proc.h>

#include "app/heading_anchor.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "render/typography.h"
#include "text/complex_text_runtime.h"
#include "text/document_font_style.h"
#include "text/shaped_text_layout.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#pragma warning(pop)

namespace mdviewer {

namespace {

constexpr float kDocumentTopPadding = 20.0f;
constexpr float kDocumentBottomPadding = 20.0f;
constexpr float kDocumentHorizontalMargin = 40.0f;
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
constexpr float kAlertTitleGap = 4.0f;
constexpr float kMetadataPaddingY = 8.0f;
constexpr float kMetadataTagFontScale = 0.78f;
constexpr float kMetadataTagPaddingX = 8.0f;
constexpr float kMetadataTagGap = 6.0f;
constexpr float kMetadataSeparatorPaddingX = 9.0f;
constexpr float kMetadataDividerWidth = 18.0f;
constexpr float kDetailsSummaryPaddingY = 5.0f;
constexpr float kDetailsSummaryIndent = 28.0f;
constexpr float kDetailsContentIndent = 20.0f;
constexpr float kDetailsContentPaddingTop = 18.0f;
constexpr float kDetailsContentPaddingBottom = 8.0f;
constexpr float kNestedBlockIndent = 20.0f;
constexpr float kNoWrapShapingWidth = 10000000.0f;

struct FlowInsets {
    float left = 0.0f;
    float right = 0.0f;
};

void AddFlowInset(
    FlowInsets& insets,
    ResolvedTextDirection direction,
    float amount) {
    if (direction == ResolvedTextDirection::RightToLeft) {
        insets.right += amount;
    } else {
        insets.left += amount;
    }
}

std::optional<ResolvedTextDirection> FindFirstBlockDirection(const Block& block) {
    for (const InlineRun& run : block.inlineRuns) {
        if (run.kind != InlineKind::Text && run.kind != InlineKind::SoftBreak) {
            continue;
        }
        const std::string_view text = run.kind == InlineKind::SoftBreak
            ? std::string_view(" ")
            : std::string_view(run.text);
        if (const auto direction = TryResolveFirstStrongDirection(text)) {
            return direction;
        }
    }
    for (const Block& child : block.children) {
        if (const auto direction = FindFirstBlockDirection(child)) {
            return direction;
        }
    }
    return std::nullopt;
}

std::vector<size_t> LegacyGraphemeBoundaries(std::string_view text) {
    std::vector<size_t> boundaries{0};
    size_t offset = 0;
    utf8proc_int32_t previous = 0;
    utf8proc_int32_t state = 0;
    bool hasPrevious = false;

    while (offset < text.size()) {
        utf8proc_int32_t current = 0;
        const utf8proc_ssize_t bytes = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
            static_cast<utf8proc_ssize_t>(text.size() - offset),
            &current);
        if (bytes <= 0) {
            ++offset;
            boundaries.push_back(offset);
            hasPrevious = false;
            state = 0;
            continue;
        }
        if (hasPrevious && utf8proc_grapheme_break_stateful(previous, current, &state)) {
            boundaries.push_back(offset);
        }
        previous = current;
        hasPrevious = true;
        offset += static_cast<size_t>(bytes);
    }
    if (boundaries.back() != text.size()) {
        boundaries.push_back(text.size());
    }
    return boundaries;
}

void PopulateLegacyCaretStops(RunLayout& run, const SkFont& font) {
    run.shaped = false;
    run.bidiLevel = 0;
    run.caretStops.clear();

    if (run.kind == InlineKind::Image) {
        return;
    }
    if (run.kind == InlineKind::Math) {
        run.caretStops.push_back({run.textStart, run.visualX});
        run.caretStops.push_back({
            run.textStart + run.text.size(),
            run.visualX + run.visualWidth,
        });
        return;
    }

    const std::vector<size_t> boundaries = LegacyGraphemeBoundaries(run.text);
    std::vector<float> cumulativeWidths(boundaries.size(), 0.0f);
    for (size_t index = 1; index < boundaries.size(); ++index) {
        const size_t previous = boundaries[index - 1];
        cumulativeWidths[index] = cumulativeWidths[index - 1] + font.measureText(
            run.text.data() + previous,
            boundaries[index] - previous,
            SkTextEncoding::kUTF8);
    }
    const float measuredWidth = cumulativeWidths.back();
    for (size_t index = 0; index < boundaries.size(); ++index) {
        const float normalizedWidth = measuredWidth > 0.0f
            ? (cumulativeWidths[index] / measuredWidth) * run.visualWidth
            : 0.0f;
        run.caretStops.push_back({
            run.textStart + boundaries[index],
            run.visualX + normalizedWidth,
        });
    }
}

bool IsBreakableWhitespace(char ch) {
    return ch == ' ' || ch == '\t';
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

std::string TrimTableCell(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

void AppendTableCellText(const Block& block, std::string& output) {
    for (const auto& run : block.inlineRuns) {
        if (run.kind == InlineKind::Text || run.kind == InlineKind::Image || run.kind == InlineKind::Math) {
            output += run.text;
        } else if (!output.empty() && output.back() != ' ') {
            output.push_back(' ');
        }
    }
    for (const auto& child : block.children) {
        AppendTableCellText(child, output);
    }
}

std::string NormalizeTableCell(const Block* cell) {
    if (cell == nullptr) {
        return {};
    }
    std::string value;
    AppendTableCellText(*cell, value);
    for (char& ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
    }
    return TrimTableCell(std::move(value));
}

std::string EscapeCsvCell(const std::string& value) {
    if (value.find_first_of(",\"") == std::string::npos) {
        return value;
    }
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::pair<std::string, std::string> SerializeTable(const Block& table) {
    std::string tsv;
    std::string csv;
    bool firstRow = true;
    for (const auto& row : table.children) {
        if (row.type != BlockType::TableRow) {
            continue;
        }
        if (!firstRow) {
            tsv += "\r\n";
            csv += "\r\n";
        }
        firstRow = false;
        bool firstCell = true;
        for (const auto& cell : row.children) {
            if (cell.type != BlockType::TableHeaderCell && cell.type != BlockType::TableCell) {
                continue;
            }
            if (!firstCell) {
                tsv.push_back('\t');
                csv.push_back(',');
            }
            firstCell = false;
            const std::string value = NormalizeTableCell(&cell);
            tsv += value;
            csv += EscapeCsvCell(value);
        }
    }
    return {std::move(tsv), std::move(csv)};
}

} // namespace

DocumentHorizontalInsets GetDocumentHorizontalInsets(float viewportWidth) {
    const float interpolation = std::clamp(
        (viewportWidth - kCompactViewportWidth) /
            (kFullMarginViewportWidth - kCompactViewportWidth),
        0.0f,
        1.0f);
    const float margin = kCompactDocumentMargin +
        ((kDocumentHorizontalMargin - kCompactDocumentMargin) * interpolation);
    return {margin, margin};
}

class LayoutContext {
public:
    float currentY = kDocumentTopPadding;
    float leftMargin;
    float rightMargin;
    float availableWidth;
    size_t currentTextOffset = 0;
    size_t nextSemanticSpanId = 1;
    std::string plainText;
    std::unordered_map<std::string, float> anchors;
    std::vector<HeadingOutlineItem> outline;
    SkFont font;
    const DocumentTypefaceSet& typefaces;
    ComplexTextRuntime complexTextRuntime;
    float baseFontSize;
    float activeFontScale = 1.0f;
    LayoutEngine::ImageSizeProvider imageSizeProvider;
    LayoutOptions options;

    LayoutContext(
        float width,
        const DocumentTypefaceSet& documentTypefaces,
        float documentBaseFontSize,
        LayoutEngine::ImageSizeProvider provider,
        LayoutOptions layoutOptions)
        : leftMargin(GetDocumentHorizontalInsets(width).left),
          rightMargin(GetDocumentHorizontalInsets(width).right),
          availableWidth(std::max(width - leftMargin - rightMargin, 1.0f)),
          typefaces(documentTypefaces),
          complexTextRuntime(sk_ref_sp(documentTypefaces.fontMgr)),
          baseFontSize(ClampBaseFontSize(documentBaseFontSize)),
          imageSizeProvider(provider),
          options(layoutOptions) {}

    void LayoutBlocks(
        const std::vector<Block>& blocks,
        std::vector<BlockLayout>& layouts,
        FlowInsets insets = {},
        std::optional<ResolvedTextDirection> inheritedListDirection = std::nullopt,
        float pendingListInset = 0.0f,
        AlertKind inheritedAlertKind = AlertKind::None) {
        for (const auto& block : blocks) {
            if (block.type == BlockType::Table) {
                LayoutTable(block, layouts, insets);
                continue;
            }

            BlockLayout bl;
            bl.type = block.type;
            bl.align = block.align;
            bl.taskListState = block.taskListState;
            bl.alertKind = block.alertKind != AlertKind::None ? block.alertKind : inheritedAlertKind;
            bl.orderedListStart = block.orderedListStart;
            bl.orderedListDelimiter = block.orderedListDelimiter;
            bl.codeLanguage = block.codeLanguage;
            bl.metadataFormat = block.metadataFormat;
            bl.detailsId = block.detailsId;
            bl.detailsOpen = block.detailsOpen;
            bl.textStart = currentTextOffset;

            const auto ownDirection = FindFirstBlockDirection(block);
            bl.direction = ownDirection.value_or(
                inheritedListDirection.value_or(ResolvedTextDirection::LeftToRight));

            FlowInsets blockInsets = insets;
            if (block.type == BlockType::ListItem) {
                AddFlowInset(
                    blockInsets,
                    bl.direction,
                    kNestedBlockIndent + pendingListInset);
            }
            const float blockLeft = leftMargin + blockInsets.left;
            const float blockWidth = std::max(
                availableWidth - blockInsets.left - blockInsets.right,
                1.0f);
            if (block.type == BlockType::CodeBlock && options.fitHorizontalOverflow) {
                const float codeViewportWidth = std::max(blockWidth - (kCodeBlockPaddingX * 2.0f), 1.0f);
                const float naturalCodeWidth = MeasureInlineRunsWidth(block.inlineRuns, BlockType::CodeBlock);
                if (naturalCodeWidth > codeViewportWidth + 0.5f) {
                    bl.fontScale = std::clamp(
                        codeViewportWidth / naturalCodeWidth,
                        options.minimumHorizontalFitScale,
                        1.0f);
                    activeFontScale = bl.fontScale;
                }
            }

            ConfigureDocumentFont(font, typefaces, block.type, InlineFormatting::None, baseFontSize);
            font.setSize(font.getSize() * activeFontScale);
            SkFontMetrics metrics;
            font.getMetrics(&metrics);
            const float lineHeight = metrics.fDescent - metrics.fAscent + metrics.fLeading;

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
            } else if (block.type == BlockType::Metadata) {
                currentY += kMetadataPaddingY;
            } else if (block.type == BlockType::Details) {
                currentY += kDetailsSummaryPaddingY;
                contentLeft += kDetailsSummaryIndent;
                contentWidth = std::max(contentWidth - kDetailsSummaryIndent, 1.0f);
            }

            if (IsHeadingBlock(block.type)) {
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
                    &laidOutContentWidth,
                    &bl.direction);
                currentY = inlineTop + inlineHeight;
                if (block.type == BlockType::Blockquote && block.alertKind != AlertKind::None) {
                    currentY += lineHeight + kAlertTitleGap;
                }
                if (block.type == BlockType::CodeBlock) {
                    bl.codeContentWidth = laidOutContentWidth;
                    bl.codeViewportWidth = contentWidth;
                    bl.horizontalContentWidth = laidOutContentWidth;
                    bl.horizontalViewportWidth = contentWidth;
                    bl.usesHorizontalScrollOffset = laidOutContentWidth > contentWidth + 0.5f;
                    bl.horizontalScrollOwnerTextStart = bl.textStart;
                }

                if (!block.children.empty() && (block.type != BlockType::Details || block.detailsOpen)) {
                    if (block.type == BlockType::Details) {
                        currentY += kDetailsContentPaddingTop;
                        if (currentTextOffset > bl.textStart && !plainText.empty() && plainText.back() != '\n') {
                            AppendPlainTextSeparator('\n');
                        }
                    }
                    FlowInsets childInsets = blockInsets;
                    const bool isListContainer =
                        block.type == BlockType::UnorderedList ||
                        block.type == BlockType::OrderedList;
                    if (!isListContainer) {
                        AddFlowInset(childInsets, bl.direction, kDetailsContentIndent);
                    }
                    LayoutBlocks(
                        block.children,
                        bl.children,
                        childInsets,
                        isListContainer
                            ? std::optional<ResolvedTextDirection>(bl.direction)
                            : block.type == BlockType::ListItem
                            ? std::optional<ResolvedTextDirection>(bl.direction)
                            : inheritedListDirection,
                        isListContainer ? kNestedBlockIndent : 0.0f,
                        bl.alertKind);
                }
            }

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockPaddingY;
            } else if (block.type == BlockType::Metadata) {
                currentY += kMetadataPaddingY;
                if (currentTextOffset > bl.textStart && !plainText.empty() && plainText.back() != '\n') {
                    AppendPlainTextSeparator('\n');
                }
            } else if (block.type == BlockType::Details) {
                currentY += block.detailsOpen
                    ? kDetailsContentPaddingBottom
                    : kDetailsSummaryPaddingY;
            }

            bl.bounds = SkRect::MakeXYWH(blockLeft, blockTop, blockWidth, currentY - blockTop);

            if (block.type == BlockType::CodeBlock) {
                currentY += kCodeBlockOuterMarginY;
            }

            bl.textLength = currentTextOffset - bl.textStart;
            layouts.push_back(std::move(bl));
            activeFontScale = 1.0f;
            currentY += kBlockSpacing;
        }
    }

private:
    static float ResolveLineX(
        float contentLeft,
        float wrapWidth,
        float lineWidth,
        TextAlign align,
        ResolvedTextDirection direction) {
        switch (align) {
            case TextAlign::Center:
                return contentLeft + std::max((wrapWidth - lineWidth) * 0.5f, 0.0f);
            case TextAlign::Right:
                return contentLeft + std::max(wrapWidth - lineWidth, 0.0f);
            case TextAlign::Left:
                return contentLeft;
            case TextAlign::Default:
            default:
                return direction == ResolvedTextDirection::RightToLeft
                    ? contentLeft + std::max(wrapWidth - lineWidth, 0.0f)
                    : contentLeft;
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
        currentLine.x = ResolveLineX(
            contentLeft,
            wrapWidth,
            lineWidth,
            align,
            currentLine.direction);
        currentLine.width = lineWidth;
        lines.push_back(std::move(currentLine));
        lineY += lines.back().height;
        currentLine = {};
        currentLine.x = contentLeft;
        currentLine.y = lineY;
        currentLine.height = baseLineHeight;
        currentLine.textStart = currentTextOffset;
    }

    std::pair<float, float> ComputeImageDisplaySize(
        const InlineRun& run,
        float wrapWidth,
        float lineHeight,
        bool isSingleImageBlock,
        BlockType blockType) {
        float actualWidth = 0.0f;
        float actualHeight = 0.0f;
        bool hasActualSize = false;
        if (imageSizeProvider) {
            const auto size = imageSizeProvider(run.imageSource);
            if (size.first > 0.0f && size.second > 0.0f) {
                actualWidth = size.first;
                actualHeight = size.second;
                hasActualSize = true;
            }
        }

        float displayWidth = 0.0f;
        float displayHeight = 0.0f;
        const bool hasRequestedWidth = run.imageRequestedWidth > 0.0f;
        const bool hasRequestedHeight = run.imageRequestedHeight > 0.0f;
        if (hasRequestedWidth || hasRequestedHeight) {
            const float aspect = hasActualSize ? actualWidth / actualHeight : 1.618f;
            displayWidth = hasRequestedWidth
                ? run.imageRequestedWidth
                : run.imageRequestedHeight * aspect;
            displayHeight = hasRequestedHeight
                ? run.imageRequestedHeight
                : run.imageRequestedWidth / aspect;
            const float fitScale = std::min(1.0f, wrapWidth / std::max(displayWidth, 1.0f));
            displayWidth = std::max(displayWidth * fitScale, 1.0f);
            displayHeight = std::max(displayHeight * fitScale, 1.0f);
        } else if (isSingleImageBlock) {
            const float maximumWidth = wrapWidth * 0.9f;
            displayWidth = hasActualSize
                ? std::min(maximumWidth, actualWidth)
                : maximumWidth;
            displayWidth = std::max(displayWidth, 1.0f);
            const float aspect = hasActualSize ? actualHeight / actualWidth : 0.618f;
            displayHeight = displayWidth * aspect;
        } else if (hasActualSize) {
            displayHeight = lineHeight * 0.8f;
            displayWidth = displayHeight * (actualWidth / actualHeight);
        } else {
            ConfigureInlineFont(blockType, run.formatting);
            displayHeight = lineHeight * 1.05f;
            const float labelWidth = font.measureText(
                run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
            displayWidth = std::clamp(labelWidth + 14.0f, 28.0f, wrapWidth);
        }
        return {displayWidth, displayHeight};
    }

    std::vector<ShapedTextInputRun> PrepareShapingRuns(
        const std::vector<InlineRun>& runs,
        float wrapWidth,
        float lineHeight,
        BlockType blockType,
        bool assignSemanticIds) {
        std::vector<ShapedTextInputRun> prepared;
        prepared.reserve(runs.size());
        const bool isSingleImageBlock = runs.size() == 1 && runs.front().kind == InlineKind::Image;
        size_t localSemanticSpanId = 1;
        for (const InlineRun& run : runs) {
            ShapedTextInputRun input;
            input.run = run;
            input.semanticSpanId = assignSemanticIds
                ? nextSemanticSpanId++
                : localSemanticSpanId++;
            if (run.kind == InlineKind::Image) {
                std::tie(input.objectWidth, input.objectHeight) = ComputeImageDisplaySize(
                    run,
                    wrapWidth,
                    lineHeight,
                    isSingleImageBlock,
                    blockType);
            } else if (run.kind == InlineKind::Math) {
                ConfigureInlineFont(blockType, run.formatting);
                input.mathLayout = LayoutMath(
                    run.mathSource,
                    run.mathDisplay,
                    font.getSize(),
                    wrapWidth);
                input.objectWidth = input.mathLayout.valid
                    ? input.mathLayout.width
                    : std::min(
                          font.measureText(
                              run.text.data(), run.text.size(), SkTextEncoding::kUTF8),
                          wrapWidth);
                input.objectHeight = input.mathLayout.valid
                    ? input.mathLayout.height
                    : lineHeight;
                input.objectWidth = std::max(input.objectWidth, 1.0f);
                input.objectHeight = std::max(input.objectHeight, 1.0f);
            }
            prepared.push_back(std::move(input));
        }
        return prepared;
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
        float* laidOutContentWidth = nullptr,
        ResolvedTextDirection* resolvedDirection = nullptr) {
        if (runs.empty()) {
            if (laidOutContentWidth != nullptr) {
                *laidOutContentWidth = 0.0f;
            }
            return 0.0f;
        }
        if (blockType == BlockType::Metadata || !complexTextRuntime.IsAvailable()) {
            if (resolvedDirection != nullptr) {
                *resolvedDirection = ResolvedTextDirection::LeftToRight;
            }
            return LayoutRunsLegacy(
                runs,
                lines,
                startY,
                contentLeft,
                wrapWidth,
                lineHeight,
                blockType,
                align,
                preserveSourceLines,
                laidOutContentWidth);
        }

        wrapWidth = std::max(wrapWidth, 1.0f);
        const std::vector<ShapedTextInputRun> prepared = PrepareShapingRuns(
            runs,
            wrapWidth,
            lineHeight,
            blockType,
            true);
        const ShapingParagraphSet paragraphs = BuildShapingParagraphs(prepared, currentTextOffset);
        std::vector<LineLayout> shapedLines;
        float lineY = startY;
        float maximumContentWidth = 0.0f;
        bool hasResolvedDirection = false;

        for (const ShapingParagraph& paragraph : paragraphs.paragraphs) {
            ShapedTextLayoutOptions shapingOptions;
            shapingOptions.blockType = blockType;
            shapingOptions.baseFontSize = baseFontSize;
            shapingOptions.fontScale = activeFontScale;
            shapingOptions.wrapWidth = preserveSourceLines ? kNoWrapShapingWidth : wrapWidth;
            shapingOptions.startY = lineY;
            shapingOptions.fallbackLineHeight = lineHeight;
            ShapedTextLayoutResult shaped = ShapeTextParagraph(
                paragraph,
                complexTextRuntime,
                typefaces,
                shapingOptions);
            if (!shaped.success) {
                return LayoutRunsLegacy(
                    runs,
                    lines,
                    startY,
                    contentLeft,
                    wrapWidth,
                    lineHeight,
                    blockType,
                    align,
                    preserveSourceLines,
                    laidOutContentWidth);
            }

            if (!hasResolvedDirection) {
                hasResolvedDirection = true;
                if (resolvedDirection != nullptr) {
                    *resolvedDirection = shaped.direction;
                }
            }
            for (LineLayout& line : shaped.lines) {
                const TextAlign lineAlign = paragraph.displayMath ? TextAlign::Center : align;
                line.x = ResolveLineX(
                    contentLeft,
                    wrapWidth,
                    line.width,
                    lineAlign,
                    line.direction);
                maximumContentWidth = std::max(maximumContentWidth, line.width);
                shapedLines.push_back(std::move(line));
            }
            lineY += shaped.totalHeight;
        }

        plainText += paragraphs.logicalText;
        currentTextOffset = paragraphs.logicalEnd;
        lines.insert(
            lines.end(),
            std::make_move_iterator(shapedLines.begin()),
            std::make_move_iterator(shapedLines.end()));
        if (laidOutContentWidth != nullptr) {
            *laidOutContentWidth = maximumContentWidth;
        }
        return lineY - startY;
    }

    float LayoutRunsLegacy(
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
        const auto measureMetadataRun = [&](const InlineRun& metadataRun) {
            ConfigureInlineFont(blockType, metadataRun.formatting);
            if (metadataRun.metadataRole == MetadataRunRole::Tag) {
                font.setSize(font.getSize() * kMetadataTagFontScale);
            }
            float visualWidth = font.measureText(
                metadataRun.text.c_str(),
                metadataRun.text.size(),
                SkTextEncoding::kUTF8);
            switch (metadataRun.metadataRole) {
                case MetadataRunRole::DotSeparator:
                    visualWidth += kMetadataSeparatorPaddingX * 2.0f;
                    break;
                case MetadataRunRole::Divider:
                    visualWidth = kMetadataDividerWidth;
                    break;
                case MetadataRunRole::Tag:
                    visualWidth += (kMetadataTagPaddingX * 2.0f) + kMetadataTagGap;
                    break;
                case MetadataRunRole::None:
                    break;
            }
            return visualWidth;
        };

        for (size_t runIndex = 0; runIndex < runs.size(); ++runIndex) {
            const auto& run = runs[runIndex];
            const size_t semanticSpanId = nextSemanticSpanId++;
            ConfigureInlineFont(blockType, run.formatting);
            if (blockType == BlockType::Metadata && run.metadataRole != MetadataRunRole::None) {
                const float visualWidth = measureMetadataRun(run);
                const bool isSeparator = run.metadataRole == MetadataRunRole::DotSeparator ||
                    run.metadataRole == MetadataRunRole::Divider;
                const float nextRunWidth = isSeparator && runIndex + 1 < runs.size()
                    ? measureMetadataRun(runs[runIndex + 1])
                    : 0.0f;

                if (currentX + visualWidth + nextRunWidth > wrapWidth && currentX > 0.0f) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                    if (isSeparator) {
                        continue;
                    }
                }

                ConfigureInlineFont(blockType, run.formatting);
                if (run.metadataRole == MetadataRunRole::Tag) {
                    font.setSize(font.getSize() * kMetadataTagFontScale);
                }

                RunLayout layoutRun{
                    .formatting = run.formatting,
                    .kind = run.kind,
                    .metadataRole = run.metadataRole,
                    .syntaxRole = run.syntaxRole,
                    .text = run.text,
                    .textStart = currentTextOffset,
                    .visualX = currentX,
                    .visualWidth = visualWidth,
                    .semanticSpanId = semanticSpanId,
                    .linkTarget = run.linkTarget,
                };
                PopulateLegacyCaretStops(layoutRun, font);
                currentLine.runs.push_back(std::move(layoutRun));
                currentLine.textLength += run.text.size();
                plainText += run.text;
                currentTextOffset += run.text.size();
                currentLine.height = std::max(currentLine.height, font.getSize() + 8.0f);
                currentLineWidth = currentX + visualWidth;
                currentX = currentLineWidth;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                continue;
            }
            if (run.kind == InlineKind::HardBreak) {
                plainText.push_back('\n');
                currentTextOffset += 1;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                currentX = 0.0f;
                currentLineWidth = 0.0f;
                continue;
            }
            if (run.kind == InlineKind::Math) {
                if (run.mathDisplay && !currentLine.runs.empty()) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }

                ConfigureInlineFont(blockType, run.formatting);
                MathLayout mathLayout = LayoutMath(
                    run.mathSource,
                    run.mathDisplay,
                    font.getSize(),
                    wrapWidth);
                const float fallbackWidth = std::min(
                    font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8),
                    wrapWidth);
                const float mathWidth = mathLayout.valid ? mathLayout.width : fallbackWidth;
                const float mathHeight = mathLayout.valid ? mathLayout.height : lineHeight;

                if (!run.mathDisplay && currentX + mathWidth > wrapWidth && currentX > 0.0f) {
                    maxContentWidth = std::max(maxContentWidth, currentLineWidth);
                    PushCurrentLine(lines, currentLine, lineY, lineHeight, contentLeft, wrapWidth, currentLineWidth, align);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }

                RunLayout layoutRun{
                    .formatting = run.formatting,
                    .kind = InlineKind::Math,
                    .syntaxRole = run.syntaxRole,
                    .text = run.text,
                    .mathSource = run.mathSource,
                    .textStart = currentTextOffset,
                    .visualX = currentX,
                    .visualWidth = mathWidth,
                    .semanticSpanId = semanticSpanId,
                    .mathDisplay = run.mathDisplay,
                    .mathLayout = std::move(mathLayout),
                    .linkTarget = run.linkTarget,
                };
                PopulateLegacyCaretStops(layoutRun, font);
                currentLine.runs.push_back(std::move(layoutRun));
                currentLine.textLength += run.text.size();
                plainText += run.text;
                currentTextOffset += run.text.size();
                currentLine.height = std::max(currentLine.height, mathHeight + (run.mathDisplay ? 10.0f : 2.0f));
                currentLineWidth = currentX + mathWidth;
                currentX = currentLineWidth;
                maxContentWidth = std::max(maxContentWidth, currentLineWidth);

                if (run.mathDisplay) {
                    PushCurrentLine(
                        lines,
                        currentLine,
                        lineY,
                        lineHeight,
                        contentLeft,
                        wrapWidth,
                        currentLineWidth,
                        TextAlign::Center);
                    currentX = 0.0f;
                    currentLineWidth = 0.0f;
                }
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
                    .visualX = currentX,
                    .visualWidth = imgDisplayW,
                    .semanticSpanId = semanticSpanId,
                    .linkTarget = run.linkTarget,
                };
                PopulateLegacyCaretStops(rl, font);
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

                const float advance = font.measureText(
                    textPtr, bytesConsumed, SkTextEncoding::kUTF8);
                RunLayout rl{
                    .formatting = run.formatting,
                    .kind = InlineKind::Text,
                    .syntaxRole = run.syntaxRole,
                    .text = std::string(textPtr, bytesConsumed),
                    .textStart = currentTextOffset,
                    .visualX = currentX,
                    .visualWidth = advance,
                    .semanticSpanId = semanticSpanId,
                    .linkTarget = run.linkTarget,
                };
                PopulateLegacyCaretStops(rl, font);
                currentLine.runs.push_back(std::move(rl));
                currentLine.textLength += bytesConsumed;
                plainText.append(textPtr, bytesConsumed);
                currentTextOffset += bytesConsumed;
                
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
            currentLine.x = ResolveLineX(
                contentLeft,
                wrapWidth,
                currentLineWidth,
                align,
                currentLine.direction);
            currentLine.width = currentLineWidth;
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
        ConfigureDocumentFont(font, typefaces, blockType, formatting, baseFontSize);
        font.setSize(font.getSize() * activeFontScale);
    }

    void AddHeadingAnchor(const Block& block, float y) {
        std::string text;
        for (const auto& run : block.inlineRuns) {
            if (run.kind == InlineKind::Text || run.kind == InlineKind::Math) {
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
        int suffix = 0;
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
        if (runs.empty()) {
            return 0.0f;
        }
        if (blockType == BlockType::Metadata || !complexTextRuntime.IsAvailable()) {
            return MeasureInlineRunsWidthLegacy(runs, blockType);
        }

        const float measurementWidth = blockType == BlockType::CodeBlock
            ? kNoWrapShapingWidth
            : kMaxTableColumnWidth;
        const float lineHeight = GetLineHeight(blockType);
        const std::vector<ShapedTextInputRun> prepared = PrepareShapingRuns(
            runs,
            measurementWidth,
            lineHeight,
            blockType,
            false);
        const ShapingParagraphSet paragraphs = BuildShapingParagraphs(prepared);
        float maximumWidth = 0.0f;
        for (const ShapingParagraph& paragraph : paragraphs.paragraphs) {
            ShapedTextLayoutOptions shapingOptions;
            shapingOptions.blockType = blockType;
            shapingOptions.baseFontSize = baseFontSize;
            shapingOptions.fontScale = activeFontScale;
            shapingOptions.wrapWidth = kNoWrapShapingWidth;
            shapingOptions.fallbackLineHeight = lineHeight;
            const ShapedTextLayoutResult shaped = ShapeTextParagraph(
                paragraph,
                complexTextRuntime,
                typefaces,
                shapingOptions);
            if (!shaped.success) {
                return MeasureInlineRunsWidthLegacy(runs, blockType);
            }
            maximumWidth = std::max(maximumWidth, shaped.naturalWidth);
        }
        return maximumWidth;
    }

    float MeasureInlineRunsWidthLegacy(
        const std::vector<InlineRun>& runs,
        BlockType blockType) {
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

            if (run.kind == InlineKind::Math) {
                ConfigureInlineFont(blockType, run.formatting);
                const MathLayout layout = LayoutMath(
                    run.mathSource,
                    run.mathDisplay,
                    font.getSize(),
                    kMaxTableColumnWidth);
                width += layout.valid
                    ? layout.width
                    : font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
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

    void LayoutTable(
        const Block& block,
        std::vector<BlockLayout>& layouts,
        FlowInsets insets) {
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
        tableLayout.direction = FindFirstBlockDirection(block).value_or(
            ResolvedTextDirection::LeftToRight);
        tableLayout.textStart = currentTextOffset;
        auto [tableTsv, tableCsv] = SerializeTable(block);
        tableLayout.tableTsv = std::move(tableTsv);
        tableLayout.tableCsv = std::move(tableCsv);

        const float tableLeft = leftMargin + insets.left;
        const float tableWidth = std::max(
            availableWidth - insets.left - insets.right,
            1.0f);
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

        std::vector<float> columnWidths = ComputeTableColumnWidths(rows, columnCount, tableWidth);
        float tableContentWidth = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0f);
        if (options.fitHorizontalOverflow && tableContentWidth > tableWidth + 0.5f) {
            tableLayout.fontScale = std::clamp(
                tableWidth / tableContentWidth,
                options.minimumHorizontalFitScale,
                1.0f);
            activeFontScale = tableLayout.fontScale;
            columnWidths = ComputeTableColumnWidths(rows, columnCount, tableWidth);
            tableContentWidth = std::accumulate(columnWidths.begin(), columnWidths.end(), 0.0f);
        }
        const bool horizontallyScrollable = tableContentWidth > tableWidth + 0.5f;
        tableLayout.horizontalContentWidth = tableContentWidth;
        tableLayout.horizontalViewportWidth = tableWidth;
        float rowTop = currentY;

        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const Block& row = *rows[rowIndex];
            BlockLayout rowLayout;
            rowLayout.type = BlockType::TableRow;
            rowLayout.direction = FindFirstBlockDirection(row).value_or(tableLayout.direction);
            rowLayout.textStart = currentTextOffset;
            rowLayout.usesHorizontalScrollOffset = horizontallyScrollable;
            rowLayout.horizontalScrollOwnerTextStart = tableLayout.textStart;
            rowLayout.fontScale = tableLayout.fontScale;

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
                cellLayout.direction = cell != nullptr
                    ? FindFirstBlockDirection(*cell).value_or(rowLayout.direction)
                    : rowLayout.direction;
                cellLayout.textStart = currentTextOffset;
                cellLayout.usesHorizontalScrollOffset = horizontallyScrollable;
                cellLayout.horizontalScrollOwnerTextStart = tableLayout.textStart;
                cellLayout.fontScale = tableLayout.fontScale;

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
                        cellAlign,
                        false,
                        nullptr,
                        &cellLayout.direction);
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

        const bool reserveScrollbarSpace = horizontallyScrollable && options.reserveHorizontalScrollbarSpace;
        currentY = rowTop + (reserveScrollbarSpace ? kHorizontalScrollbarSpace : 0.0f);
        tableLayout.bounds = SkRect::MakeXYWH(tableLeft, tableTop, tableWidth, currentY - tableTop);
        tableLayout.textLength = currentTextOffset - tableLayout.textStart;
        layouts.push_back(std::move(tableLayout));
        activeFontScale = 1.0f;
        currentY += kBlockSpacing;
    }
};

DocumentLayout LayoutEngine::ComputeLayout(
    const DocumentModel& doc,
    float width,
    const DocumentTypefaceSet& typefaces,
    float baseFontSize,
    ImageSizeProvider imageSizeProvider,
    LayoutOptions options) {
    DocumentLayout layout;
    LayoutContext ctx(width, typefaces, baseFontSize, imageSizeProvider, options);
    ctx.LayoutBlocks(doc.blocks, layout.blocks);
    layout.totalHeight = ctx.currentY + kDocumentBottomPadding;
    layout.plainText = std::move(ctx.plainText);
    layout.anchors = std::move(ctx.anchors);
    layout.outline = std::move(ctx.outline);
    return layout;
}

} // namespace mdviewer
