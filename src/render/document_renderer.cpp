#include "render/document_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/typography.h"
#include "view/document_interaction.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkData.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#pragma warning(pop)

namespace mdviewer {
namespace {

constexpr float kTextBaselineOffset = 5.0f;
constexpr float kCodeBlockPaddingX = 8.0f;
constexpr float kCodeBlockPaddingY = 8.0f;
constexpr float kBlockquoteAccentWidth = 4.0f;
constexpr float kBlockquoteTextInset = 18.0f;
constexpr float kListMarkerGap = 16.0f;
constexpr float kOrderedListMarkerTextGap = 10.0f;
constexpr float kTableCellPaddingX = 12.0f;
constexpr float kTableBorderWidth = 1.0f;

struct RenderContext {
    SkCanvas* canvas = nullptr;
    SkPaint paint;
    SkFont font;
};

size_t GetRunTextEnd(const RunLayout& run) {
    if (run.style == InlineStyle::Image) {
        return run.textStart;
    }
    return run.textStart + run.text.size();
}

float MeasureRunWidth(
    RenderContext& ctx,
    const DocumentTypefaceSet& typefaces,
    float baseFontSize,
    BlockType blockType,
    const RunLayout& run) {
    if (run.style == InlineStyle::Image) {
        return run.imageWidth;
    }

    ConfigureDocumentFont(ctx.font, typefaces, blockType, run.style, baseFontSize);
    return ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
}

void DrawCopyIcon(SkCanvas* canvas, float x, float y, float size, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1.5f);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);

    const SkRect body = SkRect::MakeXYWH(x + size * 0.2f, y + size * 0.3f, size * 0.6f, size * 0.6f);
    canvas->drawRoundRect(body, 2.0f, 2.0f, paint);

    const SkRect clip = SkRect::MakeXYWH(x + size * 0.35f, y + size * 0.15f, size * 0.3f, size * 0.25f);
    canvas->drawRoundRect(clip, 1.0f, 1.0f, paint);
}

void DrawTaskCheckbox(SkCanvas* canvas, float centerX, float centerY, float size, bool checked, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(std::max(size * 0.12f, 1.4f));
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);

    const SkRect box = SkRect::MakeXYWH(centerX - (size * 0.5f), centerY - (size * 0.5f), size, size);
    canvas->drawRoundRect(box, 2.0f, 2.0f, paint);

    if (!checked) {
        return;
    }

    canvas->drawLine(box.left() + size * 0.24f, box.top() + size * 0.53f,
                     box.left() + size * 0.43f, box.top() + size * 0.72f, paint);
    canvas->drawLine(box.left() + size * 0.43f, box.top() + size * 0.72f,
                     box.left() + size * 0.78f, box.top() + size * 0.30f, paint);
}

const LineLayout* FindFirstLine(const BlockLayout& block) {
    if (!block.lines.empty()) {
        return &block.lines.front();
    }

    for (const auto& child : block.children) {
        if (const auto* line = FindFirstLine(child)) {
            return line;
        }
    }

    return nullptr;
}

void DrawSelectionForLine(RenderContext& ctx, const DocumentSceneParams& params, const BlockLayout& block, const LineLayout& line) {
    if (!params.appState || !params.appState->HasSelection()) {
        return;
    }

    const size_t selectionStart = GetSelectionStart(*params.appState);
    const size_t selectionEnd = GetSelectionEnd(*params.appState);
    float currentX = line.x;

    for (const auto& run : line.runs) {
        const size_t runStart = run.textStart;
        const size_t runEnd = GetRunTextEnd(run);
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.type, run);

        if (selectionEnd <= runStart || selectionStart >= runEnd) {
            currentX += runWidth;
            continue;
        }

        ConfigureDocumentFont(ctx.font, params.typefaces, block.type, run.style, params.baseFontSize);
        const size_t highlightStart = std::max(selectionStart, runStart) - runStart;
        const size_t highlightEnd = std::min(selectionEnd, runEnd) - runStart;
        const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8);
        const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8);

        SkPaint highlightPaint;
        highlightPaint.setAntiAlias(true);
        highlightPaint.setColor(params.palette.selectionFill);
        ctx.canvas->drawRect(
            SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
            highlightPaint);

        currentX += runWidth;
    }
}

void DrawBlockDecoration(
    RenderContext& ctx,
    const DocumentSceneParams& params,
    const BlockLayout& block,
    BlockType parentType,
    unsigned parentOrderedListStart,
    char parentOrderedListDelimiter,
    size_t siblingIndex) {
    if (block.type == BlockType::CodeBlock) {
        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.codeBlockBackground);

        const SkRect bgRect = SkRect::MakeLTRB(
            block.bounds.left(),
            block.bounds.top() - kCodeBlockPaddingY,
            block.bounds.right() + kCodeBlockPaddingX,
            block.bounds.bottom() + kCodeBlockPaddingY);

        ctx.canvas->drawRoundRect(bgRect, 8.0f, 8.0f, backgroundPaint);

        const float btnSize = 28.0f;
        const float btnPadding = 6.0f;
        const SkRect btnRect = SkRect::MakeXYWH(
            bgRect.right() - btnSize - btnPadding,
            bgRect.top() + btnPadding,
            btnSize,
            btnSize);

        if (params.addCodeBlockButton) {
            params.addCodeBlockButton(btnRect, block.textStart, block.textStart + block.textLength);
        }

        DrawCopyIcon(ctx.canvas, btnRect.left(), btnRect.top(), btnSize, params.palette.listMarker);

        if (!block.codeLanguage.empty()) {
            ConfigureDocumentFont(ctx.font, params.typefaces, BlockType::CodeBlock, InlineStyle::Plain, params.baseFontSize);
            ctx.font.setSize(std::max(params.baseFontSize * 0.7f, 10.0f));

            SkRect labelBounds;
            ctx.font.measureText(block.codeLanguage.c_str(), block.codeLanguage.size(), SkTextEncoding::kUTF8, &labelBounds);
            const float labelPaddingX = 6.0f;
            const float labelHeight = 20.0f;
            const float labelWidth = labelBounds.width() + (labelPaddingX * 2.0f);
            const float labelRight = btnRect.left() - 6.0f;
            const SkRect labelRect = SkRect::MakeXYWH(
                std::max(bgRect.left() + 6.0f, labelRight - labelWidth),
                bgRect.top() + 6.0f,
                labelWidth,
                labelHeight);

            SkPaint labelPaint;
            labelPaint.setAntiAlias(true);
            labelPaint.setColor(params.palette.codeInlineBackground);
            ctx.canvas->drawRoundRect(labelRect, 4.0f, 4.0f, labelPaint);

            ctx.paint.setColor(params.palette.codeText);
            ctx.canvas->drawString(
                block.codeLanguage.c_str(),
                labelRect.left() + labelPaddingX,
                labelRect.top() + labelHeight - 6.0f,
                ctx.font,
                ctx.paint);
        }
        return;
    }

    if (block.type == BlockType::Blockquote) {
        SkPaint accentPaint;
        accentPaint.setAntiAlias(true);
        accentPaint.setColor(params.palette.blockquoteAccent);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                block.bounds.left(),
                block.bounds.top(),
                kBlockquoteAccentWidth,
                std::max(block.bounds.height(), 12.0f)),
            2.0f,
            2.0f,
            accentPaint);
        return;
    }

    if (block.type == BlockType::ListItem &&
        (parentType == BlockType::UnorderedList || parentType == BlockType::OrderedList)) {
        const LineLayout* firstLine = FindFirstLine(block);
        if (!firstLine) {
            return;
        }

        ConfigureDocumentFont(ctx.font, params.typefaces, BlockType::Paragraph, InlineStyle::Plain, params.baseFontSize);
        ctx.paint.setColor(params.palette.listMarker);
        const float markerBaseline = firstLine->y + firstLine->height - kTextBaselineOffset;
        const float markerX = block.bounds.left() - kListMarkerGap;
        const float markerCenterY = markerBaseline - (firstLine->height * 0.35f);

        if (block.taskListState != TaskListState::None) {
            const float boxSize = std::clamp(params.baseFontSize * 0.72f, 10.0f, 16.0f);
            DrawTaskCheckbox(
                ctx.canvas,
                markerX,
                markerCenterY,
                boxSize,
                block.taskListState == TaskListState::Checked,
                params.palette.listMarker);
        } else if (parentType == BlockType::OrderedList) {
            const std::string marker =
                std::to_string(parentOrderedListStart + static_cast<unsigned>(siblingIndex)) +
                parentOrderedListDelimiter;
            const float markerWidth = ctx.font.measureText(marker.c_str(), marker.size(), SkTextEncoding::kUTF8);
            ctx.canvas->drawString(
                marker.c_str(),
                block.bounds.left() - kOrderedListMarkerTextGap - markerWidth,
                markerBaseline,
                ctx.font,
                ctx.paint);
        } else {
            ctx.canvas->drawCircle(markerX, markerCenterY, 3.0f, ctx.paint);
        }
        return;
    }

    if (block.type == BlockType::TableHeaderCell || block.type == BlockType::TableCell) {
        SkPaint fillPaint;
        fillPaint.setAntiAlias(false);
        fillPaint.setColor(
            block.type == BlockType::TableHeaderCell
                ? params.palette.tableHeaderBackground
                : params.palette.tableCellBackground);
        ctx.canvas->drawRect(block.bounds, fillPaint);

        SkPaint borderPaint;
        borderPaint.setAntiAlias(false);
        borderPaint.setStyle(SkPaint::kStroke_Style);
        borderPaint.setStrokeWidth(kTableBorderWidth);
        borderPaint.setColor(params.palette.tableBorder);
        ctx.canvas->drawRect(block.bounds, borderPaint);
    }
}

void DrawLine(RenderContext& ctx, const DocumentSceneParams& params, const BlockLayout& block, const LineLayout& line) {
    float currentX = line.x;

    for (const auto& run : line.runs) {
        ConfigureDocumentFont(ctx.font, params.typefaces, block.type, run.style, params.baseFontSize);

        SkRect textBounds;
        const float advance = ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8, &textBounds);
        const float baselineY = std::round(line.y + line.height - kTextBaselineOffset);

        if (run.style == InlineStyle::Image && !run.url.empty()) {
            const float displayW = run.imageWidth;
            const float displayH = run.imageHeight;
            const sk_sp<SkImage> image =
                params.resolveImage ? params.resolveImage(run.url, displayW, displayH) : nullptr;
            float drawX = currentX;
            const float blockW = block.bounds.width();
            if (displayW > blockW * 0.8f) {
                drawX = block.bounds.left() + (blockW - displayW) * 0.5f;
            }

            if (image) {
                ctx.canvas->drawImageRect(
                    image,
                    SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH),
                    SkSamplingOptions(SkFilterMode::kLinear));
            } else {
                SkPaint placeholderPaint;
                placeholderPaint.setStyle(SkPaint::kStroke_Style);
                placeholderPaint.setColor(params.palette.listMarker);
                placeholderPaint.setStrokeWidth(1.0f);
                const SkRect rect = SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH);
                ctx.canvas->drawRect(rect, placeholderPaint);
                ctx.canvas->drawLine(rect.left(), rect.top(), rect.right(), rect.bottom(), placeholderPaint);
                ctx.canvas->drawLine(rect.right(), rect.top(), rect.left(), rect.bottom(), placeholderPaint);
            }

            currentX += displayW + 4.0f;
            continue;
        }

        if (run.style == InlineStyle::Code && !run.text.empty()) {
            SkPaint chipPaint;
            chipPaint.setAntiAlias(true);
            chipPaint.setColor(params.palette.codeInlineBackground);
            ctx.canvas->drawRoundRect(
                SkRect::MakeLTRB(
                    currentX - 4.0f,
                    line.y + 1.0f,
                    currentX + advance + 4.0f,
                    line.y + line.height - 1.0f),
                4.0f,
                4.0f,
                chipPaint);
        }

        ctx.paint.setColor(GetDocumentTextColor(params.palette, block.type, run.style));
        ctx.canvas->drawString(run.text.c_str(), currentX, baselineY, ctx.font, ctx.paint);

        if (run.style == InlineStyle::Link && advance > 0.0f) {
            SkPaint underlinePaint;
            underlinePaint.setAntiAlias(true);
            underlinePaint.setStrokeWidth(1.0f);
            underlinePaint.setColor(GetDocumentTextColor(params.palette, block.type, run.style));
            ctx.canvas->drawLine(currentX, baselineY + 2.0f, currentX + advance, baselineY + 2.0f, underlinePaint);
        }

        if (run.style == InlineStyle::Strikethrough && advance > 0.0f) {
            SkPaint strikePaint;
            strikePaint.setAntiAlias(true);
            strikePaint.setStrokeWidth(std::max(params.baseFontSize * 0.07f, 1.0f));
            strikePaint.setColor(GetDocumentTextColor(params.palette, block.type, run.style));
            const float strikeY = baselineY - (ctx.font.getSize() * 0.32f);
            ctx.canvas->drawLine(currentX, strikeY, currentX + advance, strikeY, strikePaint);
        }

        currentX += advance;
    }
}

void DrawBlocks(
    RenderContext& ctx,
    const DocumentSceneParams& params,
    const std::vector<BlockLayout>& blocks,
    BlockType parentType = BlockType::Paragraph,
    unsigned parentOrderedListStart = 1,
    char parentOrderedListDelimiter = '.') {
    for (size_t index = 0; index < blocks.size(); ++index) {
        const auto& block = blocks[index];
        if (block.bounds.bottom() < params.visibleDocumentTop || block.bounds.top() > params.visibleDocumentBottom) {
            continue;
        }

        if (block.type == BlockType::ThematicBreak) {
            ctx.paint.setColor(params.palette.thematicBreak);
            ctx.paint.setStrokeWidth(1.0f);
            ctx.canvas->drawLine(block.bounds.left(), block.bounds.centerY(), block.bounds.right(), block.bounds.centerY(), ctx.paint);
            continue;
        }

        DrawBlockDecoration(ctx, params, block, parentType, parentOrderedListStart, parentOrderedListDelimiter, index);

        for (const auto& line : block.lines) {
            DrawSelectionForLine(ctx, params, block, line);
            DrawLine(ctx, params, block, line);
        }

        if (!block.children.empty()) {
            DrawBlocks(ctx, params, block.children, block.type, block.orderedListStart, block.orderedListDelimiter);
        }
    }
}

void DrawAutoScrollIndicator(SkCanvas* canvas, const ThemePalette& palette, const AppState& appState) {
    if (!appState.isAutoScrolling) {
        return;
    }

    const float originX = appState.autoScrollOriginX;
    const float originY = appState.autoScrollOriginY;

    SkPaint fillPaint;
    fillPaint.setAntiAlias(true);
    fillPaint.setColor(palette.autoScrollIndicatorFill);
    canvas->drawCircle(originX, originY, 15.0f, fillPaint);

    SkPaint ringPaint;
    ringPaint.setAntiAlias(true);
    ringPaint.setColor(palette.autoScrollIndicator);
    ringPaint.setStyle(SkPaint::kStroke_Style);
    ringPaint.setStrokeWidth(1.8f);
    canvas->drawCircle(originX, originY, 15.0f, ringPaint);
    canvas->drawLine(originX - 7.0f, originY, originX + 7.0f, originY, ringPaint);
    canvas->drawLine(originX, originY - 7.0f, originX, originY + 7.0f, ringPaint);

    SkPaint arrowPaint;
    arrowPaint.setAntiAlias(true);
    arrowPaint.setColor(palette.autoScrollIndicator);
    arrowPaint.setStyle(SkPaint::kStroke_Style);
    arrowPaint.setStrokeWidth(1.8f);

    canvas->drawLine(originX - 4.0f, originY - 7.0f, originX, originY - 11.0f, arrowPaint);
    canvas->drawLine(originX, originY - 11.0f, originX + 4.0f, originY - 7.0f, arrowPaint);
    canvas->drawLine(originX - 4.0f, originY + 7.0f, originX, originY + 11.0f, arrowPaint);
    canvas->drawLine(originX, originY + 11.0f, originX + 4.0f, originY + 7.0f, arrowPaint);
}

void DrawStatusOverlays(RenderContext& ctx, const DocumentSceneParams& params) {
    if (!params.appState) {
        return;
    }

    if (!params.appState->hoveredUrl.empty()) {
        const float padding = 6.0f;
        ctx.font.setSize(GetHoverOverlayFontSize(params.baseFontSize));
        ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));

        SkRect textBounds;
        ctx.font.measureText(params.appState->hoveredUrl.c_str(), params.appState->hoveredUrl.size(), SkTextEncoding::kUTF8, &textBounds);

        const float overlayW = textBounds.width() + (padding * 2.0f);
        const float overlayH = 24.0f;
        const float overlayX = 10.0f;
        const float overlayY = params.surfaceHeight - overlayH - 10.0f;

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.menuBackground);
        backgroundPaint.setAlphaf(0.9f);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 4.0f, 4.0f, backgroundPaint);

        SkPaint borderPaint;
        borderPaint.setAntiAlias(true);
        borderPaint.setStyle(SkPaint::kStroke_Style);
        borderPaint.setStrokeWidth(1.0f);
        borderPaint.setColor(params.palette.menuSeparator);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 4.0f, 4.0f, borderPaint);

        ctx.paint.setColor(params.palette.menuText);
        ctx.canvas->drawString(params.appState->hoveredUrl.c_str(), overlayX + padding, overlayY + overlayH - 7.0f, ctx.font, ctx.paint);
    }

    if (params.appState->copiedFeedbackTimeout > params.currentTickCount) {
        const char* message = "Copied!";
        const float padding = 8.0f;
        ctx.font.setSize(GetCopiedOverlayFontSize(params.baseFontSize));
        ctx.font.setTypeface(sk_ref_sp(params.typefaces.bold));

        SkRect textBounds;
        ctx.font.measureText(message, std::strlen(message), SkTextEncoding::kUTF8, &textBounds);

        const float overlayW = textBounds.width() + (padding * 2.0f);
        const float overlayH = 28.0f;
        const float overlayX = params.surfaceWidth - overlayW - 10.0f;
        const float overlayY = params.surfaceHeight - overlayH - 10.0f;

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.menuSelectedBackground);
        backgroundPaint.setAlphaf(0.95f);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, backgroundPaint);

        ctx.paint.setColor(params.palette.menuSelectedText);
        ctx.canvas->drawString(message, overlayX + padding, overlayY + overlayH - 8.0f, ctx.font, ctx.paint);
    }
}

} // namespace

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
    InlineStyle inlineStyle,
    float baseFontSize) {
    const bool isCode = blockType == BlockType::CodeBlock || inlineStyle == InlineStyle::Code;
    const bool isHeading = IsHeadingBlock(blockType);
    const bool isStrong = inlineStyle == InlineStyle::Strong || blockType == BlockType::TableHeaderCell;
    font.setTypeface(sk_ref_sp(
        isCode ? typefaces.code : (isHeading ? typefaces.heading : (isStrong ? typefaces.bold : typefaces.regular))));
    font.setSize(
        isCode
            ? GetBlockFontSize(BlockType::CodeBlock, baseFontSize)
            : GetBlockFontSize(blockType, baseFontSize));
    font.setSubpixel(!isHeading);
    font.setHinting(SkFontHinting::kSlight);
    font.setEdging(isHeading ? SkFont::Edging::kAntiAlias : SkFont::Edging::kSubpixelAntiAlias);
    font.setEmbolden(false);
    font.setSkewX(inlineStyle == InlineStyle::Emphasis ? -0.18f : 0.0f);
    font.setScaleX(1.0f);
}

SkColor GetDocumentTextColor(const ThemePalette& palette, BlockType blockType, InlineStyle inlineStyle) {
    if (blockType == BlockType::Blockquote) {
        return palette.blockquoteText;
    }
    switch (inlineStyle) {
        case InlineStyle::SyntaxComment: return palette.syntaxComment;
        case InlineStyle::SyntaxKeyword: return palette.syntaxKeyword;
        case InlineStyle::SyntaxString: return palette.syntaxString;
        case InlineStyle::SyntaxNumber: return palette.syntaxNumber;
        case InlineStyle::SyntaxFunction: return palette.syntaxFunction;
        case InlineStyle::SyntaxType: return palette.syntaxType;
        case InlineStyle::SyntaxVariable: return palette.syntaxVariable;
        case InlineStyle::SyntaxConstant: return palette.syntaxConstant;
        case InlineStyle::SyntaxOperator: return palette.syntaxOperator;
        case InlineStyle::SyntaxPunctuation: return palette.syntaxPunctuation;
        default: break;
    }
    if (inlineStyle == InlineStyle::Code) {
        return palette.codeText;
    }
    if (inlineStyle == InlineStyle::Link) {
        return palette.linkText;
    }
    if (IsHeadingBlock(blockType)) {
        return palette.headingText;
    }
    if (blockType == BlockType::TableHeaderCell) {
        return palette.headingText;
    }
    return palette.bodyText;
}

float GetDocumentContentX(const BlockLayout& block) {
    if (block.type == BlockType::CodeBlock) {
        return block.bounds.left() + kCodeBlockPaddingX;
    }
    if (block.type == BlockType::Blockquote) {
        return block.bounds.left() + kBlockquoteTextInset;
    }
    if (block.type == BlockType::TableHeaderCell || block.type == BlockType::TableCell) {
        return block.bounds.left() + kTableCellPaddingX;
    }
    return block.bounds.left();
}

void RenderDocumentScene(const DocumentSceneParams& params) {
    if (!params.canvas || !params.appState) {
        return;
    }

    RenderContext ctx;
    ctx.canvas = params.canvas;
    ctx.paint.setAntiAlias(true);
    ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));
    ctx.font.setSubpixel(true);
    ctx.font.setHinting(SkFontHinting::kSlight);
    ctx.font.setEdging(SkFont::Edging::kSubpixelAntiAlias);

    params.canvas->save();
    params.canvas->clipRect(SkRect::MakeLTRB(0.0f, params.contentTopInset, params.surfaceWidth, params.surfaceHeight));
    params.canvas->translate(0, params.contentTopInset - params.appState->scrollOffset);

    DrawBlocks(ctx, params, params.appState->docLayout.blocks);

    if (params.appState->sourceText.empty()) {
        ctx.font.setSize(GetEmptyStateFontSize(params.baseFontSize));
        ctx.paint.setColor(params.palette.emptyStateText);
        const char* message = "Drag and drop a Markdown file here";
        SkRect bounds;
        ctx.font.measureText(message, std::strlen(message), SkTextEncoding::kUTF8, &bounds);
        const float emptyStateY = params.viewportHeight * 0.5f;
        params.canvas->drawString(message, (params.surfaceWidth - bounds.width()) / 2, emptyStateY, ctx.font, ctx.paint);
    }

    params.canvas->restore();

    if (params.scrollbarThumbRect) {
        SkPaint trackPaint;
        trackPaint.setAntiAlias(true);
        trackPaint.setColor(params.palette.scrollbarTrack);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                params.surfaceWidth - params.scrollbarWidth - params.scrollbarMargin,
                params.scrollbarMargin + params.contentTopInset,
                params.scrollbarWidth,
                std::max(params.viewportHeight - (params.scrollbarMargin * 2.0f), 1.0f)),
            5.0f,
            5.0f,
            trackPaint);

        ctx.paint.setColor(params.palette.scrollbarThumb);
        ctx.canvas->drawRoundRect(*params.scrollbarThumbRect, 5.0f, 5.0f, ctx.paint);
    }

    DrawAutoScrollIndicator(ctx.canvas, params.palette, *params.appState);
    DrawStatusOverlays(ctx, params);
}

} // namespace mdviewer
