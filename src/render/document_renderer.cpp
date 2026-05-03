#include "render/document_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "render/typography.h"
#include "view/document_interaction.h"
#include "view/document_outline.h"

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

float MeasureTextWithFallback(
    const DocumentTypefaceSet& typefaces,
    const SkFont& baseFont,
    const char* text,
    size_t length);

void DrawTextWithFallback(
    SkCanvas* canvas,
    const DocumentTypefaceSet& typefaces,
    const char* text,
    size_t length,
    float x,
    float y,
    const SkFont& baseFont,
    const SkPaint& paint);

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
    return MeasureTextWithFallback(typefaces, ctx.font, run.text.c_str(), run.text.size());
}

size_t DecodeUtf8Codepoint(const char* text, size_t length, size_t offset, uint32_t& codepoint) {
    const auto byte0 = static_cast<unsigned char>(text[offset]);
    if (byte0 < 0x80) {
        codepoint = byte0;
        return offset + 1;
    }

    auto continuation = [&](size_t index) -> uint32_t {
        if (index >= length) {
            return 0;
        }
        const auto byte = static_cast<unsigned char>(text[index]);
        return (byte & 0xC0) == 0x80 ? static_cast<uint32_t>(byte & 0x3F) : 0;
    };

    if ((byte0 & 0xE0) == 0xC0 && offset + 1 < length) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x1F) << 6) | continuation(offset + 1);
        return offset + 2;
    }
    if ((byte0 & 0xF0) == 0xE0 && offset + 2 < length) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x0F) << 12) |
                    (continuation(offset + 1) << 6) |
                    continuation(offset + 2);
        return offset + 3;
    }
    if ((byte0 & 0xF8) == 0xF0 && offset + 3 < length) {
        codepoint = (static_cast<uint32_t>(byte0 & 0x07) << 18) |
                    (continuation(offset + 1) << 12) |
                    (continuation(offset + 2) << 6) |
                    continuation(offset + 3);
        return offset + 4;
    }

    codepoint = byte0;
    return offset + 1;
}

sk_sp<SkTypeface> GetFallbackTypefaceForCodepoint(
    const DocumentTypefaceSet& typefaces,
    const SkFont& baseFont,
    uint32_t codepoint) {
    if (baseFont.unicharToGlyph(static_cast<SkUnichar>(codepoint)) != 0) {
        return sk_ref_sp(baseFont.getTypeface());
    }
    if (!typefaces.fontMgr) {
        return sk_ref_sp(baseFont.getTypeface());
    }

    SkFontStyle style = SkFontStyle::Normal();
    if (SkTypeface* baseTypeface = baseFont.getTypeface()) {
        style = baseTypeface->fontStyle();
    }

    if (auto fallback = typefaces.fontMgr->matchFamilyStyleCharacter(
            nullptr,
            style,
            nullptr,
            0,
            static_cast<SkUnichar>(codepoint))) {
        return fallback;
    }
    return sk_ref_sp(baseFont.getTypeface());
}

float MeasureTextWithFallback(
    const DocumentTypefaceSet& typefaces,
    const SkFont& baseFont,
    const char* text,
    size_t length) {
    if (!text || length == 0) {
        return 0.0f;
    }

    float width = 0.0f;
    for (size_t offset = 0; offset < length;) {
        uint32_t codepoint = 0;
        const size_t nextOffset = DecodeUtf8Codepoint(text, length, offset, codepoint);
        SkFont segmentFont = baseFont;
        segmentFont.setTypeface(GetFallbackTypefaceForCodepoint(typefaces, baseFont, codepoint));
        width += segmentFont.measureText(text + offset, nextOffset - offset, SkTextEncoding::kUTF8);
        offset = nextOffset;
    }
    return width;
}

void DrawTextWithFallback(
    SkCanvas* canvas,
    const DocumentTypefaceSet& typefaces,
    const char* text,
    size_t length,
    float x,
    float y,
    const SkFont& baseFont,
    const SkPaint& paint) {
    if (!canvas || !text || length == 0) {
        return;
    }

    float currentX = x;
    for (size_t offset = 0; offset < length;) {
        uint32_t codepoint = 0;
        const size_t nextOffset = DecodeUtf8Codepoint(text, length, offset, codepoint);
        SkFont segmentFont = baseFont;
        segmentFont.setTypeface(GetFallbackTypefaceForCodepoint(typefaces, baseFont, codepoint));
        canvas->drawSimpleText(text + offset, nextOffset - offset, SkTextEncoding::kUTF8, currentX, y, segmentFont, paint);
        currentX += segmentFont.measureText(text + offset, nextOffset - offset, SkTextEncoding::kUTF8);
        offset = nextOffset;
    }
}

size_t NextUtf8Offset(const std::string& text, size_t offset) {
    if (offset >= text.size()) {
        return text.size();
    }

    ++offset;
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
        ++offset;
    }
    return offset;
}

size_t FitUtf8TextBytes(const DocumentTypefaceSet& typefaces, const SkFont& font, const std::string& text, float maxWidth) {
    size_t bestOffset = 0;
    for (size_t offset = 0; offset <= text.size();) {
        const float width = MeasureTextWithFallback(typefaces, font, text.c_str(), offset);
        if (width > maxWidth) {
            break;
        }

        bestOffset = offset;
        if (offset == text.size()) {
            break;
        }
        offset = NextUtf8Offset(text, offset);
    }
    return bestOffset;
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
        const bool isCodeText = block.type == BlockType::CodeBlock || run.style == InlineStyle::Code;
        const float highlightPaddingX = isCodeText ? 2.5f : 0.0f;
        const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8) - highlightPaddingX;
        const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8) + highlightPaddingX;

        SkPaint highlightPaint;
        highlightPaint.setAntiAlias(true);
        highlightPaint.setColor(params.palette.selectionFill);
        ctx.canvas->drawRoundRect(
            SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
            isCodeText ? 3.0f : 1.0f,
            isCodeText ? 3.0f : 1.0f,
            highlightPaint);

        currentX += runWidth;
    }
}

void DrawInlineDecorationsForLine(RenderContext& ctx, const DocumentSceneParams& params, const BlockLayout& block, const LineLayout& line) {
    float currentX = line.x;

    for (const auto& run : line.runs) {
        ConfigureDocumentFont(ctx.font, params.typefaces, block.type, run.style, params.baseFontSize);
        const float advance = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.type, run);

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

        currentX += advance;
    }
}

void DrawSearchForLine(RenderContext& ctx, const DocumentSceneParams& params, const BlockLayout& block, const LineLayout& line) {
    if (!params.appState || !params.appState->searchActive || params.appState->searchMatches.empty()) {
        return;
    }

    const auto currentMatch = GetCurrentSearchMatch(*params.appState);
    float currentX = line.x;

    for (const auto& run : line.runs) {
        const size_t runStart = run.textStart;
        const size_t runEnd = GetRunTextEnd(run);
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.type, run);

        if (runEnd <= runStart || run.style == InlineStyle::Image) {
            currentX += runWidth;
            continue;
        }

        ConfigureDocumentFont(ctx.font, params.typefaces, block.type, run.style, params.baseFontSize);
        for (const auto& match : params.appState->searchMatches) {
            if (match.second <= runStart || match.first >= runEnd) {
                continue;
            }

            const size_t highlightStart = std::max(match.first, runStart) - runStart;
            const size_t highlightEnd = std::min(match.second, runEnd) - runStart;
            if (highlightStart >= highlightEnd) {
                continue;
            }

            const bool isCodeText = block.type == BlockType::CodeBlock || run.style == InlineStyle::Code;
            const float highlightPaddingX = isCodeText ? 2.5f : 0.0f;
            const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8) - highlightPaddingX;
            const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8) + highlightPaddingX;

            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(
                currentMatch && currentMatch->first == match.first && currentMatch->second == match.second
                    ? params.palette.menuSelectedBackground
                    : params.palette.selectionFill);
            ctx.canvas->drawRoundRect(
                SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
                3.0f,
                3.0f,
                highlightPaint);
        }

        currentX += runWidth;
    }
}

void DrawSearchStrokeForLine(RenderContext& ctx, const DocumentSceneParams& params, const BlockLayout& block, const LineLayout& line) {
    if (!params.appState || !params.appState->searchActive || params.appState->searchMatches.empty()) {
        return;
    }

    const auto currentMatch = GetCurrentSearchMatch(*params.appState);
    float currentX = line.x;

    for (const auto& run : line.runs) {
        const size_t runStart = run.textStart;
        const size_t runEnd = GetRunTextEnd(run);
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.type, run);

        if (runEnd <= runStart || run.style == InlineStyle::Image) {
            currentX += runWidth;
            continue;
        }

        ConfigureDocumentFont(ctx.font, params.typefaces, block.type, run.style, params.baseFontSize);
        for (const auto& match : params.appState->searchMatches) {
            if (match.second <= runStart || match.first >= runEnd) {
                continue;
            }

            const size_t highlightStart = std::max(match.first, runStart) - runStart;
            const size_t highlightEnd = std::min(match.second, runEnd) - runStart;
            if (highlightStart >= highlightEnd) {
                continue;
            }

            const bool isCodeText = block.type == BlockType::CodeBlock || run.style == InlineStyle::Code;
            const float highlightPaddingX = isCodeText ? 2.5f : 0.0f;
            const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8) - highlightPaddingX;
            const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8) + highlightPaddingX;
            const bool isCurrent = currentMatch && currentMatch->first == match.first && currentMatch->second == match.second;

            SkPaint strokePaint;
            strokePaint.setAntiAlias(true);
            strokePaint.setStyle(SkPaint::kStroke_Style);
            strokePaint.setStrokeWidth(isCurrent ? 1.8f : 1.1f);
            strokePaint.setColor(isCurrent ? params.palette.linkText : params.palette.menuDisabledText);
            ctx.canvas->drawRoundRect(
                SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
                3.0f,
                3.0f,
                strokePaint);
        }

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

        const float advance = MeasureTextWithFallback(params.typefaces, ctx.font, run.text.c_str(), run.text.size());
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

                if (!run.text.empty() && rect.width() > 24.0f && rect.height() > 16.0f) {
                    ctx.canvas->save();
                    ctx.canvas->clipRect(rect.makeInset(4.0f, 2.0f));
                    ctx.paint.setColor(params.palette.emptyStateText);
                    ctx.canvas->drawString(
                        run.text.c_str(),
                        rect.left() + 5.0f,
                        rect.centerY() + (ctx.font.getSize() * 0.35f),
                        ctx.font,
                        ctx.paint);
                    ctx.canvas->restore();
                }
            }

            currentX += displayW + 4.0f;
            continue;
        }

        ctx.paint.setColor(GetDocumentTextColor(params.palette, block.type, run.style));
        DrawTextWithFallback(
            ctx.canvas,
            params.typefaces,
            run.text.c_str(),
            run.text.size(),
            currentX,
            baselineY,
            ctx.font,
            ctx.paint);

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
            DrawInlineDecorationsForLine(ctx, params, block, line);
            DrawSearchForLine(ctx, params, block, line);
            DrawSelectionForLine(ctx, params, block, line);
            DrawLine(ctx, params, block, line);
            DrawSearchStrokeForLine(ctx, params, block, line);
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

    if (params.appState->searchActive) {
        const float paddingX = 10.0f;
        const float overlayH = 34.0f;
        const float overlayW = std::min(430.0f, std::max(240.0f, params.surfaceWidth - 28.0f));
        const float overlayX = params.surfaceWidth - overlayW - 14.0f;
        const float overlayY = params.contentTopInset + 10.0f;
        const float closeSize = 22.0f;
        const SkRect closeRect = SkRect::MakeXYWH(
            overlayX + overlayW - closeSize - 6.0f,
            overlayY + (overlayH - closeSize) * 0.5f,
            closeSize,
            closeSize);
        const_cast<AppState*>(params.appState)->searchCloseButtonRect = closeRect;

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.menuBackground);
        backgroundPaint.setAlphaf(0.96f);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, backgroundPaint);

        SkPaint borderPaint;
        borderPaint.setAntiAlias(true);
        borderPaint.setStyle(SkPaint::kStroke_Style);
        borderPaint.setStrokeWidth(1.0f);
        borderPaint.setColor(params.palette.menuSeparator);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, borderPaint);

        ctx.font.setTypeface(sk_ref_sp(params.typefaces.bold));
        ctx.font.setSize(std::max(params.baseFontSize * 0.78f, 12.0f));
        ctx.paint.setColor(params.palette.menuDisabledText);
        const char* label = "Find";
        ctx.canvas->drawString(label, overlayX + paddingX, overlayY + 22.0f, ctx.font, ctx.paint);

        ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));
        ctx.paint.setColor(params.palette.menuText);
        const std::string query = params.appState->searchQuery.empty() ? std::string() : params.appState->searchQuery;
        const char* placeholder = "type to search";
        const bool hasQuery = !query.empty();
        if (!hasQuery) {
            ctx.paint.setColor(params.palette.menuDisabledText);
        }

        const float queryX = overlayX + 58.0f;
        const float countW = 108.0f;
        ctx.canvas->save();
        ctx.canvas->clipRect(SkRect::MakeLTRB(queryX, overlayY + 4.0f, closeRect.left() - countW, overlayY + overlayH - 4.0f));
        const std::string displayQuery = hasQuery ? query : placeholder;
        ctx.canvas->drawString(displayQuery.c_str(), queryX, overlayY + 22.0f, ctx.font, ctx.paint);

        if (hasQuery) {
            SkRect queryBounds;
            ctx.font.measureText(displayQuery.c_str(), displayQuery.size(), SkTextEncoding::kUTF8, &queryBounds);
            const float caretX = queryX + queryBounds.width() + 2.0f;
            SkPaint caretPaint;
            caretPaint.setAntiAlias(false);
            caretPaint.setColor(params.palette.menuText);
            ctx.canvas->drawRect(SkRect::MakeXYWH(caretX, overlayY + 9.0f, 1.0f, 17.0f), caretPaint);
        }
        ctx.canvas->restore();

        ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));
        ctx.font.setSize(std::max(params.baseFontSize * 0.72f, 11.0f));
        const std::string countText = params.appState->searchQuery.empty()
            ? "0/0"
            : (params.appState->searchMatches.empty()
                ? "0/0"
                : std::to_string(params.appState->currentSearchMatch + 1) + "/" + std::to_string(params.appState->searchMatches.size()));
        SkRect countBounds;
        ctx.font.measureText(countText.c_str(), countText.size(), SkTextEncoding::kUTF8, &countBounds);
        ctx.paint.setColor(params.appState->searchMatches.empty() && !params.appState->searchQuery.empty()
            ? params.palette.codeText
            : params.palette.menuDisabledText);
        ctx.canvas->drawString(
            countText.c_str(),
            closeRect.left() - 10.0f - countBounds.width(),
            overlayY + 22.0f,
            ctx.font,
            ctx.paint);

        SkPaint closePaint;
        closePaint.setAntiAlias(true);
        closePaint.setColor(params.palette.menuSelectedBackground);
        closePaint.setAlphaf(0.55f);
        ctx.canvas->drawRoundRect(closeRect, 4.0f, 4.0f, closePaint);

        SkPaint closeStroke;
        closeStroke.setAntiAlias(true);
        closeStroke.setColor(params.palette.menuText);
        closeStroke.setStrokeWidth(1.6f);
        closeStroke.setStrokeCap(SkPaint::kRound_Cap);
        const float cx = closeRect.centerX();
        const float cy = closeRect.centerY();
        const float arm = 5.2f;
        ctx.canvas->drawLine(cx - arm, cy - arm, cx + arm, cy + arm, closeStroke);
        ctx.canvas->drawLine(cx + arm, cy - arm, cx - arm, cy + arm, closeStroke);
    } else {
        const_cast<AppState*>(params.appState)->searchCloseButtonRect = SkRect::MakeEmpty();
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

    const bool showZoomFeedback = params.appState->zoomFeedbackTimeout > params.currentTickCount;
    if (showZoomFeedback) {
        char message[32] = {};
        std::snprintf(message, sizeof(message), "%.0f pt", params.appState->zoomFeedbackFontSize);
        const float padding = 10.0f;
        ctx.font.setSize(GetCopiedOverlayFontSize(params.baseFontSize));
        ctx.font.setTypeface(sk_ref_sp(params.typefaces.bold));

        SkRect textBounds;
        ctx.font.measureText(message, std::strlen(message), SkTextEncoding::kUTF8, &textBounds);

        const float overlayW = textBounds.width() + (padding * 2.0f);
        const float overlayH = 30.0f;
        const float overlayX = params.surfaceWidth - overlayW - 10.0f;
        const float overlayY = params.surfaceHeight - overlayH - 10.0f;

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.menuSelectedBackground);
        backgroundPaint.setAlphaf(0.95f);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, backgroundPaint);

        ctx.paint.setColor(params.palette.menuSelectedText);
        ctx.canvas->drawString(message, overlayX + padding, overlayY + overlayH - 9.0f, ctx.font, ctx.paint);
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
        const float overlayY = params.surfaceHeight - overlayH - (showZoomFeedback ? 48.0f : 10.0f);

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(true);
        backgroundPaint.setColor(params.palette.menuSelectedBackground);
        backgroundPaint.setAlphaf(0.95f);
        ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, backgroundPaint);

        ctx.paint.setColor(params.palette.menuSelectedText);
        ctx.canvas->drawString(message, overlayX + padding, overlayY + overlayH - 8.0f, ctx.font, ctx.paint);
    }
}

void DrawOutlineSidebar(RenderContext& ctx, const DocumentSceneParams& params) {
    if (params.appState->docLayout.outline.empty() || params.documentLeftInset <= 0.0f) {
        return;
    }

    const float sidebarX = GetOutlineX(*params.appState, params.surfaceWidth);
    const SkRect sidebarRect = SkRect::MakeXYWH(
        sidebarX,
        params.contentTopInset,
        params.documentLeftInset,
        params.surfaceHeight - params.contentTopInset);

    SkPaint backgroundPaint;
    backgroundPaint.setAntiAlias(false);
    backgroundPaint.setColor(params.palette.menuBackground);
    ctx.canvas->drawRect(sidebarRect, backgroundPaint);

    SkPaint borderPaint;
    borderPaint.setAntiAlias(false);
    borderPaint.setColor(params.palette.menuSeparator);
    ctx.canvas->drawRect(
        SkRect::MakeXYWH(
            params.appState->outlineSide == OutlineSide::Left ? sidebarRect.right() - 1.0f : sidebarRect.left(),
            params.contentTopInset,
            1.0f,
            sidebarRect.height()),
        borderPaint);

    ctx.canvas->save();
    ctx.canvas->clipRect(sidebarRect);

    const size_t currentIndex = GetCurrentOutlineIndex(params.appState->docLayout, params.visibleDocumentTop);
    const size_t focusedIndex = std::min(
        params.appState->focusedOutlineIndex,
        params.appState->docLayout.outline.empty() ? 0 : params.appState->docLayout.outline.size() - 1);
    ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));
    ctx.font.setSize(15.0f);
    ctx.font.setSubpixel(true);

    auto drawSidebarToggleIcon = [&](const SkRect& rect, bool collapsed) {
        SkPaint iconPaint;
        iconPaint.setAntiAlias(true);
        iconPaint.setColor(params.palette.menuText);
        iconPaint.setStyle(SkPaint::kStroke_Style);
        iconPaint.setStrokeWidth(2.0f);
        iconPaint.setStrokeCap(SkPaint::kRound_Cap);
        iconPaint.setStrokeJoin(SkPaint::kRound_Join);

        const float iconSize = 21.0f;
        const float iconX = rect.left() + std::max((rect.width() - iconSize) * 0.5f, 0.0f);
        const float iconY = rect.top() + std::max((rect.height() - iconSize) * 0.5f, 0.0f);
        const SkRect outer = SkRect::MakeXYWH(iconX, iconY, iconSize, iconSize);
        ctx.canvas->drawRoundRect(outer, 3.5f, 3.5f, iconPaint);

        iconPaint.setStrokeWidth(2.4f);
        const float caretCenterX = outer.centerX();
        const float caretCenterY = outer.centerY();
        const bool pointsRight = params.appState->outlineSide == OutlineSide::Left ? collapsed : !collapsed;
        if (pointsRight) {
            ctx.canvas->drawLine(caretCenterX - 2.5f, caretCenterY - 5.0f, caretCenterX + 2.5f, caretCenterY, iconPaint);
            ctx.canvas->drawLine(caretCenterX + 2.5f, caretCenterY, caretCenterX - 2.5f, caretCenterY + 5.0f, iconPaint);
        } else {
            ctx.canvas->drawLine(caretCenterX + 2.5f, caretCenterY - 5.0f, caretCenterX - 2.5f, caretCenterY, iconPaint);
            ctx.canvas->drawLine(caretCenterX - 2.5f, caretCenterY, caretCenterX + 2.5f, caretCenterY + 5.0f, iconPaint);
        }
    };

    const float toggleSize = 24.0f;
    const float expandedToggleX = params.appState->outlineSide == OutlineSide::Right
        ? 6.0f
        : std::max(params.documentLeftInset - toggleSize - 6.0f, 6.0f);
    const float toggleX = sidebarX + (params.appState->outlineCollapsed ? 5.0f : expandedToggleX);
    const SkRect toggleRect = SkRect::MakeXYWH(toggleX, params.contentTopInset + 5.0f, toggleSize, toggleSize);
    SkPaint togglePaint;
    togglePaint.setAntiAlias(true);
    togglePaint.setColor(params.palette.menuSelectedBackground);
    togglePaint.setAlphaf(params.appState->outlineCollapsed ? 0.25f : 0.16f);
    ctx.canvas->drawRoundRect(toggleRect, 5.0f, 5.0f, togglePaint);

    drawSidebarToggleIcon(toggleRect, params.appState->outlineCollapsed);

    if (params.appState->outlineCollapsed) {
        ctx.canvas->restore();
        return;
    }

    ctx.font.setSize(15.0f);
    const float textTop = params.contentTopInset + kOutlineHeaderHeight + kOutlineTopPadding;
    for (size_t index = 0; index < params.appState->docLayout.outline.size(); ++index) {
        const HeadingOutlineItem& item = params.appState->docLayout.outline[index];
        const float itemY = textTop + (static_cast<float>(index) * kOutlineItemHeight);
        if (itemY > params.surfaceHeight) {
            break;
        }

        const float localIndent = 14.0f + (static_cast<float>(std::clamp(item.level, 1, 6) - 1) * 14.0f);
        const float textX = sidebarX + localIndent;
        const SkRect itemRect = SkRect::MakeXYWH(sidebarX + 8.0f, itemY, params.documentLeftInset - 16.0f, kOutlineItemHeight);
        if (index == currentIndex || (params.appState->outlineFocused && index == focusedIndex)) {
            SkPaint selectedPaint;
            selectedPaint.setAntiAlias(true);
            selectedPaint.setColor(params.palette.menuSelectedBackground);
            selectedPaint.setAlphaf(params.appState->outlineFocused && index == focusedIndex ? 0.75f : 0.45f);
            ctx.canvas->drawRoundRect(itemRect, 5.0f, 5.0f, selectedPaint);
        }

        const std::string fallbackText = "(untitled)";
        const std::string& text = item.text.empty() ? fallbackText : item.text;
        const float maxTextWidth = std::max(params.documentLeftInset - localIndent - 16.0f, 16.0f);
        const size_t bytesToDraw = FitUtf8TextBytes(params.typefaces, ctx.font, text, maxTextWidth);

        ctx.paint.setColor(index == currentIndex || (params.appState->outlineFocused && index == focusedIndex)
            ? params.palette.menuSelectedText
            : params.palette.menuText);
        DrawTextWithFallback(
            ctx.canvas,
            params.typefaces,
            text.c_str(),
            bytesToDraw,
            textX,
            itemY + 21.0f,
            ctx.font,
            ctx.paint);
    }

    ctx.canvas->restore();
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

    DrawOutlineSidebar(ctx, params);

    const float documentTranslateX = params.appState->outlineSide == OutlineSide::Left ? params.documentLeftInset : 0.0f;
    const float documentClipLeft = params.appState->outlineSide == OutlineSide::Left ? params.documentLeftInset : 0.0f;
    const float documentClipRight = params.appState->outlineSide == OutlineSide::Right
        ? params.surfaceWidth - params.documentLeftInset
        : params.surfaceWidth;

    params.canvas->save();
    params.canvas->clipRect(SkRect::MakeLTRB(documentClipLeft, params.contentTopInset, documentClipRight, params.surfaceHeight));
    params.canvas->translate(documentTranslateX, params.contentTopInset - params.appState->scrollOffset);

    DrawBlocks(ctx, params, params.appState->docLayout.blocks);

    if (params.appState->sourceText.empty()) {
        ctx.font.setSize(GetEmptyStateFontSize(params.baseFontSize));
        ctx.paint.setColor(params.palette.emptyStateText);
        const char* message = "Drag and drop a Markdown file here";
        SkRect bounds;
        ctx.font.measureText(message, std::strlen(message), SkTextEncoding::kUTF8, &bounds);
        const float emptyStateY = params.viewportHeight * 0.5f;
        params.canvas->drawString(
            message,
            documentClipLeft + ((documentClipRight - documentClipLeft - bounds.width()) / 2.0f),
            emptyStateY,
            ctx.font,
            ctx.paint);
    }

    params.canvas->restore();

    if (params.scrollbarThumbRect) {
        const float scrollbarTrackX = params.appState->outlineSide == OutlineSide::Right
            ? params.surfaceWidth - params.documentLeftInset - params.scrollbarWidth - params.scrollbarMargin
            : params.surfaceWidth - params.scrollbarWidth - params.scrollbarMargin;
        SkPaint trackPaint;
        trackPaint.setAntiAlias(true);
        trackPaint.setColor(params.palette.scrollbarTrack);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                scrollbarTrackX,
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
