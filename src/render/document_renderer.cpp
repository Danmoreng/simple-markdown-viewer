#include "render/document_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

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
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#pragma warning(pop)

namespace mdviewer {
namespace {

constexpr float kTextBaselineOffset = 5.0f;
constexpr float kCodeBlockPaddingX = 8.0f;
constexpr float kCodeBlockPaddingY = 8.0f;
constexpr float kHorizontalScrollbarHeight = 6.0f;
constexpr float kHorizontalScrollbarMinThumbWidth = 28.0f;
constexpr float kBlockquoteAccentWidth = 4.0f;
constexpr float kBlockquoteTextInset = 18.0f;
constexpr float kListMarkerGap = 16.0f;
constexpr float kOrderedListMarkerTextGap = 10.0f;
constexpr float kTableCellPaddingX = 12.0f;
constexpr float kTableBorderWidth = 1.0f;
constexpr float kMetadataTagFontScale = 0.78f;
constexpr float kMetadataTagPaddingX = 8.0f;
constexpr float kMetadataTagGap = 6.0f;

const char* GetAlertTitle(AlertKind kind) {
    switch (kind) {
        case AlertKind::Note: return "Note";
        case AlertKind::Tip: return "Tip";
        case AlertKind::Important: return "Important";
        case AlertKind::Warning: return "Warning";
        case AlertKind::Caution: return "Caution";
        case AlertKind::None:
        default: return "";
    }
}

SkColor GetAlertColor(AlertKind kind, const ThemePalette& palette) {
    const int backgroundBrightness =
        static_cast<int>(SkColorGetR(palette.windowBackground)) +
        static_cast<int>(SkColorGetG(palette.windowBackground)) +
        static_cast<int>(SkColorGetB(palette.windowBackground));
    const bool dark = backgroundBrightness < (128 * 3);
    switch (kind) {
        case AlertKind::Note: return dark ? SkColorSetRGB(68, 147, 248) : SkColorSetRGB(9, 105, 218);
        case AlertKind::Tip: return dark ? SkColorSetRGB(63, 185, 80) : SkColorSetRGB(26, 127, 55);
        case AlertKind::Important: return dark ? SkColorSetRGB(171, 125, 248) : SkColorSetRGB(130, 80, 223);
        case AlertKind::Warning: return dark ? SkColorSetRGB(210, 153, 34) : SkColorSetRGB(154, 103, 0);
        case AlertKind::Caution: return dark ? SkColorSetRGB(248, 81, 73) : SkColorSetRGB(209, 36, 47);
        case AlertKind::None:
        default: return palette.blockquoteAccent;
    }
}

void DrawAlertExclamation(SkCanvas* canvas, float centerX, float top, SkPaint& paint) {
    paint.setStrokeWidth(1.8f);
    canvas->drawLine(centerX, top, centerX, top + 5.0f, paint);
    paint.setStyle(SkPaint::kFill_Style);
    canvas->drawCircle(centerX, top + 8.0f, 1.1f, paint);
}

void DrawAlertIcon(SkCanvas* canvas, AlertKind kind, float x, float y, float size, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(1.7f);
    const float centerX = x + (size * 0.5f);
    const float centerY = y + (size * 0.5f);

    switch (kind) {
        case AlertKind::Note:
            canvas->drawCircle(centerX, centerY, size * 0.42f, paint);
            canvas->drawLine(centerX, y + 7.0f, centerX, y + 12.0f, paint);
            paint.setStyle(SkPaint::kFill_Style);
            canvas->drawCircle(centerX, y + 4.5f, 1.1f, paint);
            break;
        case AlertKind::Tip:
            canvas->drawCircle(centerX, y + 6.5f, 4.7f, paint);
            canvas->drawLine(x + 6.0f, y + 11.0f, x + 6.0f, y + 13.0f, paint);
            canvas->drawLine(x + 10.0f, y + 11.0f, x + 10.0f, y + 13.0f, paint);
            canvas->drawLine(x + 6.0f, y + 13.5f, x + 10.0f, y + 13.5f, paint);
            break;
        case AlertKind::Important: {
            const SkRect bubble = SkRect::MakeXYWH(x + 1.0f, y + 1.5f, size - 2.0f, size - 5.0f);
            canvas->drawRoundRect(bubble, 2.0f, 2.0f, paint);
            canvas->drawLine(x + 5.0f, bubble.bottom(), x + 4.0f, y + size - 1.0f, paint);
            canvas->drawLine(x + 4.0f, y + size - 1.0f, x + 8.0f, bubble.bottom(), paint);
            DrawAlertExclamation(canvas, centerX, y + 4.0f, paint);
            break;
        }
        case AlertKind::Warning:
            canvas->drawLine(centerX, y + 1.0f, x + size - 1.0f, y + size - 1.0f, paint);
            canvas->drawLine(x + size - 1.0f, y + size - 1.0f, x + 1.0f, y + size - 1.0f, paint);
            canvas->drawLine(x + 1.0f, y + size - 1.0f, centerX, y + 1.0f, paint);
            DrawAlertExclamation(canvas, centerX, y + 5.0f, paint);
            break;
        case AlertKind::Caution: {
            const float inset = size * 0.28f;
            const SkPoint points[] = {
                {x + inset, y}, {x + size - inset, y}, {x + size, y + inset},
                {x + size, y + size - inset}, {x + size - inset, y + size},
                {x + inset, y + size}, {x, y + size - inset}, {x, y + inset},
            };
            for (size_t index = 0; index < 8; ++index) {
                canvas->drawLine(points[index], points[(index + 1) % 8], paint);
            }
            DrawAlertExclamation(canvas, centerX, y + 4.0f, paint);
            break;
        }
        case AlertKind::None:
            break;
    }
}

bool IsRemoteImageSource(std::string_view source) {
    return source.starts_with("https://") || source.starts_with("http://");
}

struct RenderContext {
    SkCanvas* canvas = nullptr;
    SkPaint paint;
    SkFont font;
};

size_t GetRunTextEnd(const RunLayout& run) {
    if (run.kind == InlineKind::Image) {
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

void ConfigureBlockFont(
    SkFont& font,
    const DocumentSceneParams& params,
    const BlockLayout& block,
    BlockType blockType,
    InlineFormatting formatting) {
    ConfigureDocumentFont(font, params.typefaces, blockType, formatting, params.baseFontSize);
    font.setSize(font.getSize() * block.fontScale);
}

float MeasureRunWidth(
    RenderContext& ctx,
    const DocumentTypefaceSet& typefaces,
    float baseFontSize,
    float fontScale,
    BlockType blockType,
    const RunLayout& run) {
    if (run.kind == InlineKind::Image) {
        return run.imageWidth;
    }
    if (run.visualWidth > 0.0f) {
        return run.visualWidth;
    }

    ConfigureDocumentFont(ctx.font, typefaces, blockType, run.formatting, baseFontSize);
    ctx.font.setSize(ctx.font.getSize() * fontScale);
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
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.fontScale, block.type, run);

        if (selectionEnd <= runStart || selectionStart >= runEnd) {
            currentX += runWidth;
            continue;
        }

        ConfigureBlockFont(ctx.font, params, block, block.type, run.formatting);
        const size_t highlightStart = std::max(selectionStart, runStart) - runStart;
        const size_t highlightEnd = std::min(selectionEnd, runEnd) - runStart;
        const bool isCodeText = block.type == BlockType::CodeBlock ||
            HasFormatting(run.formatting, InlineFormatting::Code) ||
            HasFormatting(run.formatting, InlineFormatting::Keyboard);
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
        ConfigureBlockFont(ctx.font, params, block, block.type, run.formatting);
        const float advance = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.fontScale, block.type, run);

        if (HasFormatting(run.formatting, InlineFormatting::Code) && !run.text.empty()) {
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
        if (HasFormatting(run.formatting, InlineFormatting::Keyboard) && !run.text.empty()) {
            const SkRect keyRect = SkRect::MakeLTRB(
                currentX - 3.0f,
                line.y + 2.0f,
                currentX + advance + 3.0f,
                line.y + line.height - 2.0f);
            SkPaint keyPaint;
            keyPaint.setAntiAlias(true);
            keyPaint.setColor(params.palette.codeInlineBackground);
            ctx.canvas->drawRoundRect(keyRect, 3.0f, 3.0f, keyPaint);

            SkPaint keyBorderPaint;
            keyBorderPaint.setAntiAlias(true);
            keyBorderPaint.setStyle(SkPaint::kStroke_Style);
            keyBorderPaint.setStrokeWidth(1.0f);
            keyBorderPaint.setColor(params.palette.tableBorder);
            ctx.canvas->drawRoundRect(keyRect, 3.0f, 3.0f, keyBorderPaint);
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
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.fontScale, block.type, run);

        if (runEnd <= runStart || run.kind == InlineKind::Image) {
            currentX += runWidth;
            continue;
        }

        ConfigureBlockFont(ctx.font, params, block, block.type, run.formatting);
        for (const auto& match : params.appState->searchMatches) {
            if (match.second <= runStart || match.first >= runEnd) {
                continue;
            }

            const size_t highlightStart = std::max(match.first, runStart) - runStart;
            const size_t highlightEnd = std::min(match.second, runEnd) - runStart;
            if (highlightStart >= highlightEnd) {
                continue;
            }

            const bool isCodeText = block.type == BlockType::CodeBlock ||
                HasFormatting(run.formatting, InlineFormatting::Code) ||
                HasFormatting(run.formatting, InlineFormatting::Keyboard);
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
        const float runWidth = MeasureRunWidth(ctx, params.typefaces, params.baseFontSize, block.fontScale, block.type, run);

        if (runEnd <= runStart || run.kind == InlineKind::Image) {
            currentX += runWidth;
            continue;
        }

        ConfigureBlockFont(ctx.font, params, block, block.type, run.formatting);
        for (const auto& match : params.appState->searchMatches) {
            if (match.second <= runStart || match.first >= runEnd) {
                continue;
            }

            const size_t highlightStart = std::max(match.first, runStart) - runStart;
            const size_t highlightEnd = std::min(match.second, runEnd) - runStart;
            if (highlightStart >= highlightEnd) {
                continue;
            }

            const bool isCodeText = block.type == BlockType::CodeBlock ||
                HasFormatting(run.formatting, InlineFormatting::Code) ||
                HasFormatting(run.formatting, InlineFormatting::Keyboard);
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
    if (block.type == BlockType::Details && !block.lines.empty()) {
        const float summaryBottom = block.lines.back().y + block.lines.back().height + 5.0f;
        const SkRect cardRect = block.bounds;
        const SkRect summaryRect = SkRect::MakeLTRB(
            block.bounds.left(),
            block.bounds.top(),
            block.bounds.right(),
            summaryBottom);

        SkPaint cardPaint;
        cardPaint.setAntiAlias(true);
        cardPaint.setColor(params.palette.tableCellBackground);
        ctx.canvas->drawRoundRect(cardRect, 6.0f, 6.0f, cardPaint);

        SkPaint summaryPaint;
        summaryPaint.setAntiAlias(true);
        summaryPaint.setColor(params.palette.menuSelectedBackground);
        summaryPaint.setAlphaf(0.52f);
        if (block.detailsOpen) {
            const SkVector radii[] = {
                {6.0f, 6.0f},
                {6.0f, 6.0f},
                {0.0f, 0.0f},
                {0.0f, 0.0f},
            };
            SkRRect summaryShape;
            summaryShape.setRectRadii(summaryRect, radii);
            ctx.canvas->drawRRect(summaryShape, summaryPaint);
        } else {
            ctx.canvas->drawRoundRect(summaryRect, 6.0f, 6.0f, summaryPaint);
        }

        SkPaint cardBorderPaint;
        cardBorderPaint.setAntiAlias(true);
        cardBorderPaint.setStyle(SkPaint::kStroke_Style);
        cardBorderPaint.setStrokeWidth(1.0f);
        cardBorderPaint.setColor(params.palette.tableBorder);
        ctx.canvas->drawRoundRect(cardRect, 6.0f, 6.0f, cardBorderPaint);
        if (block.detailsOpen) {
            ctx.canvas->drawLine(
                summaryRect.left(),
                summaryRect.bottom() - 0.5f,
                summaryRect.right(),
                summaryRect.bottom() - 0.5f,
                cardBorderPaint);
        }

        SkPaint chevronPaint;
        chevronPaint.setAntiAlias(true);
        chevronPaint.setColor(params.palette.bodyText);
        chevronPaint.setStyle(SkPaint::kStroke_Style);
        chevronPaint.setStrokeWidth(2.0f);
        chevronPaint.setStrokeCap(SkPaint::kRound_Cap);
        chevronPaint.setStrokeJoin(SkPaint::kRound_Join);
        const float centerX = block.bounds.left() + 14.0f;
        const float centerY = summaryRect.centerY();
        if (block.detailsOpen) {
            ctx.canvas->drawLine(centerX - 4.0f, centerY - 2.0f, centerX, centerY + 2.0f, chevronPaint);
            ctx.canvas->drawLine(centerX, centerY + 2.0f, centerX + 4.0f, centerY - 2.0f, chevronPaint);
        } else {
            ctx.canvas->drawLine(centerX - 2.0f, centerY - 4.0f, centerX + 2.0f, centerY, chevronPaint);
            ctx.canvas->drawLine(centerX + 2.0f, centerY, centerX - 2.0f, centerY + 4.0f, chevronPaint);
        }
        return;
    }

    if (block.type == BlockType::Metadata) {
        SkPaint rulePaint;
        rulePaint.setAntiAlias(true);
        rulePaint.setStrokeWidth(1.0f);
        rulePaint.setColor(params.palette.tableBorder);
        ctx.canvas->drawLine(
            block.bounds.left(),
            block.bounds.bottom() - 0.5f,
            block.bounds.right(),
            block.bounds.bottom() - 0.5f,
            rulePaint);
        return;
    }

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

        const bool showControls = params.showInteractiveElements;
        const float btnSize = 28.0f;
        const float btnPadding = 6.0f;
        const SkRect btnRect = SkRect::MakeXYWH(
            bgRect.right() - btnSize - btnPadding,
            bgRect.top() + btnPadding,
            btnSize,
            btnSize);

        if (showControls && params.addCodeBlockButton) {
            params.addCodeBlockButton(btnRect, block.textStart, block.textStart + block.textLength);
        }

        if (showControls) {
            DrawCopyIcon(ctx.canvas, btnRect.left(), btnRect.top(), btnSize, params.palette.listMarker);
        }

        if (!block.codeLanguage.empty()) {
            std::string codeLabel = block.codeLanguage;
            if (block.codeHighlightStatus == syntax::HighlightStatus::TimedOut ||
                block.codeHighlightStatus == syntax::HighlightStatus::Failed) {
                codeLabel += " · plain";
            }

            const float blockBaseFontSize = params.baseFontSize * block.fontScale;
            ConfigureBlockFont(ctx.font, params, block, BlockType::CodeBlock, InlineFormatting::None);
            ctx.font.setSize(std::max(blockBaseFontSize * 0.7f, 7.0f));

            SkRect labelBounds;
            ctx.font.measureText(codeLabel.c_str(), codeLabel.size(), SkTextEncoding::kUTF8, &labelBounds);
            const float labelPaddingX = 6.0f;
            const float labelHeight = 20.0f;
            const float labelWidth = labelBounds.width() + (labelPaddingX * 2.0f);
            const float labelRight = showControls ? btnRect.left() - 6.0f : bgRect.right() - 6.0f;
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
                codeLabel.c_str(),
                labelRect.left() + labelPaddingX,
                labelRect.top() + labelHeight - 6.0f,
                ctx.font,
                ctx.paint);
        }
        return;
    }

    if (block.type == BlockType::Blockquote) {
        const SkColor accentColor = GetAlertColor(block.alertKind, params.palette);
        SkPaint accentPaint;
        accentPaint.setAntiAlias(true);
        accentPaint.setColor(accentColor);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                block.bounds.left(),
                block.bounds.top(),
                kBlockquoteAccentWidth,
                std::max(block.bounds.height(), 12.0f)),
            2.0f,
            2.0f,
            accentPaint);
        if (block.alertKind != AlertKind::None) {
            const float iconSize = 16.0f;
            const float titleLeft = block.bounds.left() + kBlockquoteTextInset;
            DrawAlertIcon(ctx.canvas, block.alertKind, titleLeft, block.bounds.top() + 1.0f, iconSize, accentColor);

            ConfigureBlockFont(
                ctx.font,
                params,
                block,
                BlockType::Paragraph,
                InlineFormatting::Strong);
            ctx.font.setSize(std::max(params.baseFontSize * block.fontScale * 0.84f, 12.0f));
            ctx.paint.setColor(accentColor);
            const char* title = GetAlertTitle(block.alertKind);
            ctx.canvas->drawString(
                title,
                titleLeft + iconSize + 7.0f,
                block.bounds.top() + ctx.font.getSize() + 1.0f,
                ctx.font,
                ctx.paint);
        }
        return;
    }

    if (block.type == BlockType::ListItem &&
        (parentType == BlockType::UnorderedList || parentType == BlockType::OrderedList)) {
        const LineLayout* firstLine = FindFirstLine(block);
        if (!firstLine) {
            return;
        }

        const float blockBaseFontSize = params.baseFontSize * block.fontScale;
        ConfigureBlockFont(ctx.font, params, block, BlockType::Paragraph, InlineFormatting::None);
        ctx.paint.setColor(params.palette.listMarker);
        const float markerBaseline = firstLine->y + firstLine->height - kTextBaselineOffset;
        const float markerX = block.bounds.left() - kListMarkerGap;
        const float markerCenterY = markerBaseline - (firstLine->height * 0.35f);

        if (block.taskListState != TaskListState::None) {
            const float boxSize = std::clamp(blockBaseFontSize * 0.72f, 10.0f, 16.0f);
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
        ConfigureBlockFont(ctx.font, params, block, block.type, run.formatting);
        if (run.metadataRole == MetadataRunRole::Tag) {
            ctx.font.setSize(ctx.font.getSize() * kMetadataTagFontScale);
        }

        const float textAdvance = MeasureTextWithFallback(params.typefaces, ctx.font, run.text.c_str(), run.text.size());
        const float advance = run.visualWidth > 0.0f ? run.visualWidth : textAdvance;
        float baselineY = std::round(line.y + line.height - kTextBaselineOffset);
        if (HasFormatting(run.formatting, InlineFormatting::Superscript)) {
            baselineY -= ctx.font.getSize() * 0.38f;
        } else if (HasFormatting(run.formatting, InlineFormatting::Subscript)) {
            baselineY += ctx.font.getSize() * 0.2f;
        }

        if (run.kind == InlineKind::Image && !run.imageSource.empty()) {
            const float displayW = run.imageWidth;
            const float displayH = run.imageHeight;
            const sk_sp<SkImage> image =
                params.resolveImage ? params.resolveImage(run.imageSource, displayW, displayH) : nullptr;
            const float renderedWidth = image && params.freezeImageDimensions
                ? static_cast<float>(image->width())
                : displayW;
            const float renderedHeight = image && params.freezeImageDimensions
                ? static_cast<float>(image->height())
                : displayH;
            float layoutX = currentX;
            const float blockW = block.bounds.width();
            if (displayW > blockW * 0.8f) {
                layoutX = block.bounds.left() + (blockW - displayW) * 0.5f;
            }
            const float layoutY = line.y + (line.height - displayH) / 2.0f;
            const SkRect imageLayoutRect = SkRect::MakeXYWH(layoutX, layoutY, displayW, displayH);

            if (image) {
                const bool imageAlreadyMatchesDisplaySize =
                    std::abs(renderedWidth - static_cast<float>(image->width())) <= 0.5f &&
                    std::abs(renderedHeight - static_cast<float>(image->height())) <= 0.5f;
                if (params.freezeImageDimensions) {
                    const float frozenX = imageLayoutRect.centerX() - renderedWidth * 0.5f;
                    const float frozenY = imageLayoutRect.centerY() - renderedHeight * 0.5f;
                    ctx.canvas->save();
                    ctx.canvas->clipRect(imageLayoutRect);
                    ctx.canvas->drawImage(image, frozenX, frozenY);
                    ctx.canvas->restore();
                } else if (imageAlreadyMatchesDisplaySize) {
                    ctx.canvas->drawImage(image, layoutX, layoutY);
                } else {
                    ctx.canvas->drawImageRect(
                        image,
                        imageLayoutRect,
                        SkSamplingOptions(SkFilterMode::kLinear));
                }
            } else {
                SkPaint placeholderPaint;
                placeholderPaint.setColor(params.palette.listMarker);
                placeholderPaint.setStrokeWidth(1.0f);
                const SkRect rect = imageLayoutRect;
                if (IsRemoteImageSource(run.imageSource)) {
                    SkPaint fillPaint;
                    fillPaint.setAntiAlias(true);
                    fillPaint.setColor(params.palette.codeInlineBackground);
                    ctx.canvas->drawRoundRect(rect, 4.0f, 4.0f, fillPaint);
                    placeholderPaint.setAntiAlias(true);
                    placeholderPaint.setStyle(SkPaint::kStroke_Style);
                    ctx.canvas->drawRoundRect(rect, 4.0f, 4.0f, placeholderPaint);
                } else {
                    placeholderPaint.setStyle(SkPaint::kStroke_Style);
                    ctx.canvas->drawRect(rect, placeholderPaint);
                    ctx.canvas->drawLine(rect.left(), rect.top(), rect.right(), rect.bottom(), placeholderPaint);
                    ctx.canvas->drawLine(rect.right(), rect.top(), rect.left(), rect.bottom(), placeholderPaint);
                }

                if (!run.text.empty() && rect.width() > 24.0f && rect.height() > 12.0f) {
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

        if (run.metadataRole == MetadataRunRole::Divider) {
            SkPaint dividerPaint;
            dividerPaint.setAntiAlias(true);
            dividerPaint.setStrokeWidth(1.0f);
            dividerPaint.setColor(params.palette.tableBorder);
            const float dividerX = currentX + (advance * 0.5f);
            const float halfHeight = std::min(line.height * 0.3f, 8.0f);
            ctx.canvas->drawLine(
                dividerX,
                line.y + (line.height * 0.5f) - halfHeight,
                dividerX,
                line.y + (line.height * 0.5f) + halfHeight,
                dividerPaint);
            currentX += advance;
            continue;
        }

        if (run.metadataRole == MetadataRunRole::Tag) {
            const float pillWidth = std::max(advance - kMetadataTagGap, 1.0f);
            const float pillHeight = std::min(std::max(ctx.font.getSize() + 6.0f, 18.0f), line.height - 2.0f);
            const SkRect pillRect = SkRect::MakeXYWH(
                currentX,
                line.y + ((line.height - pillHeight) * 0.5f),
                pillWidth,
                pillHeight);
            SkPaint pillPaint;
            pillPaint.setAntiAlias(true);
            pillPaint.setColor(params.palette.codeInlineBackground);
            ctx.canvas->drawRoundRect(pillRect, pillHeight * 0.5f, pillHeight * 0.5f, pillPaint);

            SkPaint pillBorder;
            pillBorder.setAntiAlias(true);
            pillBorder.setStyle(SkPaint::kStroke_Style);
            pillBorder.setStrokeWidth(1.0f);
            pillBorder.setColor(params.palette.tableBorder);
            ctx.canvas->drawRoundRect(pillRect, pillHeight * 0.5f, pillHeight * 0.5f, pillBorder);

            SkFontMetrics tagMetrics;
            ctx.font.getMetrics(&tagMetrics);
            const float tagBaselineY = pillRect.centerY() -
                ((tagMetrics.fAscent + tagMetrics.fDescent) * 0.5f);
            ctx.paint.setColor(params.palette.bodyText);
            DrawTextWithFallback(
                ctx.canvas,
                params.typefaces,
                run.text.c_str(),
                run.text.size(),
                currentX + kMetadataTagPaddingX,
                tagBaselineY,
                ctx.font,
                ctx.paint);
            currentX += advance;
            continue;
        }

        const bool isLink = !run.linkTarget.empty();
        ctx.paint.setColor(run.metadataRole == MetadataRunRole::DotSeparator
            ? params.palette.listMarker
            : GetDocumentTextColor(
                params.palette,
                block.type,
                run.formatting,
                run.syntaxRole,
                isLink));
        DrawTextWithFallback(
            ctx.canvas,
            params.typefaces,
            run.text.c_str(),
            run.text.size(),
            currentX + (run.metadataRole == MetadataRunRole::DotSeparator
                ? std::max((advance - textAdvance) * 0.5f, 0.0f)
                : 0.0f),
            baselineY,
            ctx.font,
            ctx.paint);

        if (isLink && textAdvance > 0.0f) {
            SkPaint underlinePaint;
            underlinePaint.setAntiAlias(true);
            underlinePaint.setStrokeWidth(1.0f);
            underlinePaint.setColor(GetDocumentTextColor(
                params.palette,
                block.type,
                run.formatting,
                run.syntaxRole,
                true));
            ctx.canvas->drawLine(currentX, baselineY + 2.0f, currentX + textAdvance, baselineY + 2.0f, underlinePaint);
        }

        if (HasFormatting(run.formatting, InlineFormatting::Strikethrough) && textAdvance > 0.0f) {
            SkPaint strikePaint;
            strikePaint.setAntiAlias(true);
            strikePaint.setStrokeWidth(std::max(params.baseFontSize * block.fontScale * 0.07f, 1.0f));
            strikePaint.setColor(GetDocumentTextColor(
                params.palette,
                block.type,
                run.formatting,
                run.syntaxRole,
                isLink));
            const float strikeY = baselineY - (ctx.font.getSize() * 0.32f);
            ctx.canvas->drawLine(currentX, strikeY, currentX + textAdvance, strikeY, strikePaint);
        }

        currentX += advance;
    }
}

float GetHorizontalScrollOffset(const DocumentSceneParams& params, const BlockLayout& block) {
    if (!params.appState) {
        return 0.0f;
    }
    const auto found = params.appState->horizontalScrollOffsets.find(block.textStart);
    if (found == params.appState->horizontalScrollOffsets.end()) {
        return 0.0f;
    }
    return std::clamp(
        found->second,
        0.0f,
        std::max(block.horizontalContentWidth - block.horizontalViewportWidth, 0.0f));
}

void DrawHorizontalScrollbar(
    RenderContext& ctx,
    const DocumentSceneParams& params,
    const BlockLayout& block) {
    const float maxScroll = std::max(block.horizontalContentWidth - block.horizontalViewportWidth, 0.0f);
    if (maxScroll <= 0.5f || block.horizontalViewportWidth <= 0.0f) {
        return;
    }

    const float trackLeft = block.type == BlockType::CodeBlock
        ? block.bounds.left() + kCodeBlockPaddingX
        : block.bounds.left();
    const float trackTop = block.bounds.bottom() - (kHorizontalScrollbarHeight * 0.5f);
    const SkRect trackRect = SkRect::MakeXYWH(
        trackLeft,
        trackTop,
        block.horizontalViewportWidth,
        kHorizontalScrollbarHeight);
    const float thumbWidth = std::clamp(
        block.horizontalViewportWidth * (block.horizontalViewportWidth / block.horizontalContentWidth),
        std::min(kHorizontalScrollbarMinThumbWidth, block.horizontalViewportWidth),
        block.horizontalViewportWidth);
    const float thumbTravel = std::max(trackRect.width() - thumbWidth, 0.0f);
    const float scrollOffset = GetHorizontalScrollOffset(params, block);
    const float thumbLeft = trackRect.left() + (maxScroll > 0.0f ? (scrollOffset / maxScroll) * thumbTravel : 0.0f);
    const SkRect thumbRect = SkRect::MakeXYWH(
        thumbLeft,
        trackRect.top(),
        thumbWidth,
        trackRect.height());

    SkPaint trackPaint;
    trackPaint.setAntiAlias(true);
    trackPaint.setColor(params.palette.scrollbarTrack);
    ctx.canvas->drawRoundRect(trackRect, kHorizontalScrollbarHeight * 0.5f, kHorizontalScrollbarHeight * 0.5f, trackPaint);

    SkPaint thumbPaint;
    thumbPaint.setAntiAlias(true);
    thumbPaint.setColor(params.palette.scrollbarThumb);
    ctx.canvas->drawRoundRect(thumbRect, kHorizontalScrollbarHeight * 0.5f, kHorizontalScrollbarHeight * 0.5f, thumbPaint);

    if (params.showInteractiveElements && params.addHorizontalScrollbar) {
        params.addHorizontalScrollbar(HorizontalScrollbarRegion{
            .viewportRect = SkRect::MakeLTRB(
                block.bounds.left(),
                block.bounds.top() - (block.type == BlockType::CodeBlock ? kCodeBlockPaddingY : 0.0f),
                block.bounds.right() + (block.type == BlockType::CodeBlock ? kCodeBlockPaddingX : 0.0f),
                block.bounds.bottom() + (block.type == BlockType::CodeBlock ? kCodeBlockPaddingY : 0.0f)),
            .trackRect = trackRect,
            .thumbRect = thumbRect,
            .blockTextStart = block.textStart,
            .maxScroll = maxScroll,
        });
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

        const bool isHorizontallyScrollable =
            block.horizontalContentWidth > block.horizontalViewportWidth + 0.5f;
        if (isHorizontallyScrollable) {
            const float clipLeft = block.type == BlockType::CodeBlock
                ? block.bounds.left() + kCodeBlockPaddingX
                : block.bounds.left();
            const float clipTop = block.type == BlockType::CodeBlock
                ? block.bounds.top() - kCodeBlockPaddingY
                : block.bounds.top();
            ctx.canvas->save();
            ctx.canvas->clipRect(SkRect::MakeXYWH(
                clipLeft,
                clipTop,
                block.horizontalViewportWidth,
                block.bounds.height() + (block.type == BlockType::CodeBlock ? kCodeBlockPaddingY * 2.0f : 0.0f)));
            ctx.canvas->translate(-GetHorizontalScrollOffset(params, block), 0.0f);
        }

        for (const auto& line : block.lines) {
            DrawInlineDecorationsForLine(ctx, params, block, line);
            DrawSearchForLine(ctx, params, block, line);
            DrawSelectionForLine(ctx, params, block, line);
            DrawLine(ctx, params, block, line);
            DrawSearchStrokeForLine(ctx, params, block, line);
        }

        if (!block.children.empty() && isHorizontallyScrollable) {
            DrawBlocks(ctx, params, block.children, block.type, block.orderedListStart, block.orderedListDelimiter);
        }

        if (isHorizontallyScrollable) {
            ctx.canvas->restore();
            if (params.showInteractiveElements) {
                DrawHorizontalScrollbar(ctx, params, block);
            }
        }

        if (!block.children.empty() && !isHorizontallyScrollable) {
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
    const float dividerX = GetOutlineDividerX(*params.appState, params.surfaceWidth);
    const float dividerDrawX = params.appState->outlineSide == OutlineSide::Left
        ? dividerX - 1.0f
        : dividerX;
    ctx.canvas->drawRect(
        SkRect::MakeXYWH(
            dividerDrawX,
            params.contentTopInset,
            1.0f,
            sidebarRect.height()),
        borderPaint);

    if (!params.appState->outlineCollapsed) {
        SkPaint handlePaint = borderPaint;
        handlePaint.setAlphaf(0.55f);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                dividerX - 1.5f,
                params.contentTopInset + std::max((sidebarRect.height() - 44.0f) * 0.5f, 0.0f),
                3.0f,
                44.0f),
            1.5f,
            1.5f,
            handlePaint);
    }

    const size_t currentIndex = GetCurrentOutlineIndex(params.appState->docLayout, params.visibleDocumentTop);
    const size_t focusedIndex = std::min(
        params.appState->focusedOutlineIndex,
        params.appState->docLayout.outline.empty() ? 0 : params.appState->docLayout.outline.size() - 1);
    ctx.font.setTypeface(sk_ref_sp(params.typefaces.regular));
    ctx.font.setSize(15.0f);
    ctx.font.setSubpixel(true);
    const bool toggleHovered = params.showInteractiveElements && params.appState->outlineToggleHovered;

    auto drawSidebarToggleIcon = [&](const SkRect& rect, bool collapsed) {
        SkPaint iconPaint;
        iconPaint.setAntiAlias(true);
        iconPaint.setColor(toggleHovered ? params.palette.menuSelectedText : params.palette.menuText);
        iconPaint.setStyle(SkPaint::kStroke_Style);
        iconPaint.setStrokeWidth(2.2f);
        iconPaint.setStrokeCap(SkPaint::kRound_Cap);
        iconPaint.setStrokeJoin(SkPaint::kRound_Join);

        const float caretCenterX = rect.centerX();
        const float caretCenterY = rect.centerY();
        const bool pointsRight = params.appState->outlineSide == OutlineSide::Left ? collapsed : !collapsed;
        if (pointsRight) {
            ctx.canvas->drawLine(caretCenterX - 2.5f, caretCenterY - 4.5f, caretCenterX + 2.0f, caretCenterY, iconPaint);
            ctx.canvas->drawLine(caretCenterX + 2.0f, caretCenterY, caretCenterX - 2.5f, caretCenterY + 4.5f, iconPaint);
        } else {
            ctx.canvas->drawLine(caretCenterX + 2.5f, caretCenterY - 4.5f, caretCenterX - 2.0f, caretCenterY, iconPaint);
            ctx.canvas->drawLine(caretCenterX - 2.0f, caretCenterY, caretCenterX + 2.5f, caretCenterY + 4.5f, iconPaint);
        }
    };

    const SkRect toggleRect = GetOutlineToggleRect(
        *params.appState,
        params.surfaceWidth,
        params.contentTopInset);
    const SkColor toggleColor = toggleHovered
        ? SkColorSetRGB(
            static_cast<uint8_t>((SkColorGetR(params.palette.menuSelectedBackground) * 3 +
                SkColorGetR(params.palette.menuSelectedText)) / 4),
            static_cast<uint8_t>((SkColorGetG(params.palette.menuSelectedBackground) * 3 +
                SkColorGetG(params.palette.menuSelectedText)) / 4),
            static_cast<uint8_t>((SkColorGetB(params.palette.menuSelectedBackground) * 3 +
                SkColorGetB(params.palette.menuSelectedText)) / 4))
        : params.palette.menuSelectedBackground;
    SkPaint togglePaint;
    togglePaint.setAntiAlias(true);
    togglePaint.setColor(toggleColor);
    togglePaint.setAlphaf(toggleHovered ? 1.0f : 0.78f);
    ctx.canvas->drawCircle(toggleRect.centerX(), toggleRect.centerY(), toggleRect.width() * 0.5f, togglePaint);

    SkPaint toggleBorderPaint;
    toggleBorderPaint.setAntiAlias(true);
    toggleBorderPaint.setStyle(SkPaint::kStroke_Style);
    toggleBorderPaint.setStrokeWidth(1.0f);
    toggleBorderPaint.setColor(toggleHovered
        ? params.palette.menuText
        : params.palette.menuSeparator);
    ctx.canvas->drawCircle(
        toggleRect.centerX(),
        toggleRect.centerY(),
        (toggleRect.width() * 0.5f) - 0.5f,
        toggleBorderPaint);

    drawSidebarToggleIcon(toggleRect, params.appState->outlineCollapsed);

    if (params.appState->outlineCollapsed) {
        return;
    }

    ctx.font.setSize(15.0f);
    const float textTop = params.contentTopInset + kOutlineTopPadding;
    const float contentGap = 6.0f;
    const float contentLeft = params.appState->outlineSide == OutlineSide::Right
        ? std::max(sidebarRect.left() + 8.0f, toggleRect.right() + contentGap)
        : sidebarRect.left() + 8.0f;
    const float contentRight = params.appState->outlineSide == OutlineSide::Left
        ? std::min(sidebarRect.right() - 8.0f, toggleRect.left() - contentGap)
        : sidebarRect.right() - 8.0f;
    ctx.canvas->save();
    ctx.canvas->clipRect(SkRect::MakeLTRB(
        contentLeft,
        textTop,
        contentRight,
        params.surfaceHeight - kOutlineBottomPadding));
    for (size_t index = 0; index < params.appState->docLayout.outline.size(); ++index) {
        const HeadingOutlineItem& item = params.appState->docLayout.outline[index];
        const float itemY = textTop + (static_cast<float>(index) * kOutlineItemHeight) -
            params.appState->outlineScrollOffset;
        if (itemY + kOutlineItemHeight < textTop) {
            continue;
        }
        if (itemY > params.surfaceHeight) {
            break;
        }

        const float localIndent = 6.0f + (static_cast<float>(std::clamp(item.level, 1, 6) - 1) * 14.0f);
        const float textX = contentLeft + localIndent;
        const SkRect itemRect = SkRect::MakeLTRB(contentLeft, itemY, contentRight, itemY + kOutlineItemHeight);
        if (index == currentIndex || (params.appState->outlineFocused && index == focusedIndex)) {
            SkPaint selectedPaint;
            selectedPaint.setAntiAlias(true);
            selectedPaint.setColor(params.palette.menuSelectedBackground);
            selectedPaint.setAlphaf(params.appState->outlineFocused && index == focusedIndex ? 0.75f : 0.45f);
            ctx.canvas->drawRoundRect(itemRect, 5.0f, 5.0f, selectedPaint);
        }

        const std::string fallbackText = "(untitled)";
        const std::string& text = item.text.empty() ? fallbackText : item.text;
        const float maxTextWidth = std::max(
            contentRight - textX - kOutlineScrollbarMargin - kOutlineScrollbarWidth,
            16.0f);
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

    if (const auto thumbRect = GetOutlineScrollbarThumbRect(
            *params.appState,
            params.surfaceWidth,
            params.surfaceHeight,
            params.contentTopInset)) {
        SkPaint trackPaint;
        trackPaint.setAntiAlias(true);
        trackPaint.setColor(params.palette.menuSeparator);
        trackPaint.setAlphaf(0.35f);
        ctx.canvas->drawRoundRect(
            SkRect::MakeXYWH(
                thumbRect->left(),
                textTop,
                kOutlineScrollbarWidth,
                GetOutlineViewportHeight(params.surfaceHeight, params.contentTopInset)),
            kOutlineScrollbarWidth * 0.5f,
            kOutlineScrollbarWidth * 0.5f,
            trackPaint);

        SkPaint thumbPaint;
        thumbPaint.setAntiAlias(true);
        thumbPaint.setColor(params.palette.menuText);
        thumbPaint.setAlphaf(params.appState->isDraggingOutlineScrollbar ? 0.85f : 0.6f);
        ctx.canvas->drawRoundRect(
            *thumbRect,
            kOutlineScrollbarWidth * 0.5f,
            kOutlineScrollbarWidth * 0.5f,
            thumbPaint);
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

SkColor GetDocumentTextColor(
    const ThemePalette& palette,
    BlockType blockType,
    InlineFormatting formatting,
    SyntaxRole syntaxRole,
    bool isLink) {
    if (blockType == BlockType::Blockquote) {
        return palette.blockquoteText;
    }
    switch (syntaxRole) {
        case SyntaxRole::Comment: return palette.syntaxComment;
        case SyntaxRole::Keyword: return palette.syntaxKeyword;
        case SyntaxRole::String: return palette.syntaxString;
        case SyntaxRole::Number: return palette.syntaxNumber;
        case SyntaxRole::Function: return palette.syntaxFunction;
        case SyntaxRole::Type: return palette.syntaxType;
        case SyntaxRole::Variable: return palette.syntaxVariable;
        case SyntaxRole::Constant: return palette.syntaxConstant;
        case SyntaxRole::Operator: return palette.syntaxOperator;
        case SyntaxRole::Punctuation: return palette.syntaxPunctuation;
        default: break;
    }
    if (HasFormatting(formatting, InlineFormatting::Code) ||
        HasFormatting(formatting, InlineFormatting::Keyboard)) {
        return palette.codeText;
    }
    if (isLink) {
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

    // The outline toggle straddles the document/sidebar divider. Draw the
    // sidebar after the document scrollbar so the circular button remains a
    // clean, uninterrupted control where the two surfaces meet.
    DrawOutlineSidebar(ctx, params);

    if (params.showInteractiveElements) {
        DrawAutoScrollIndicator(ctx.canvas, params.palette, *params.appState);
        DrawStatusOverlays(ctx, params);
    }
}

} // namespace mdviewer
