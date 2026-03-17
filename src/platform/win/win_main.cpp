#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <filesystem>
#include <iostream>
#include <vector>

#include "app/app_state.h"
#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "util/file_io.h"
#include "util/skia_font_utils.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#pragma warning(pop)

namespace {
    mdviewer::AppState g_appState;
    sk_sp<SkSurface> g_surface;
    sk_sp<SkTypeface> g_typeface;
    sk_sp<SkTypeface> g_headingTypeface;
    sk_sp<SkTypeface> g_codeTypeface;
    sk_sp<SkFontMgr> g_fontMgr;

    constexpr float kTextBaselineOffset = 5.0f;
    constexpr float kCodeBlockPaddingX = 12.0f;
    constexpr float kCodeBlockPaddingY = 8.0f;
    constexpr float kBlockquoteAccentWidth = 4.0f;
    constexpr float kBlockquoteTextInset = 18.0f;
    constexpr float kListMarkerGap = 16.0f;
    constexpr float kScrollbarWidth = 10.0f;
    constexpr float kScrollbarMargin = 4.0f;

    struct RenderContext {
        SkCanvas* canvas;
        SkPaint paint;
        SkFont font;
    };

    struct TextHit {
        size_t position = 0;
        bool valid = false;
    };

    bool EnsureFontSystem() {
        if (!g_fontMgr) {
            g_fontMgr = mdviewer::CreateFontManager();
        }

        if (!g_typeface) {
            g_typeface = mdviewer::CreateDefaultTypeface(g_fontMgr);
        }

        if (!g_headingTypeface && g_fontMgr) {
            g_headingTypeface = g_fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Bold());
            if (!g_headingTypeface) {
                g_headingTypeface = g_fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Bold());
            }
            if (!g_headingTypeface) {
                g_headingTypeface = g_typeface;
            }
        }

        if (!g_codeTypeface) {
            g_codeTypeface = mdviewer::CreatePreferredTypeface(
                g_fontMgr,
                {"Cascadia Mono", "Consolas", "JetBrains Mono", "Courier New"});
            if (!g_codeTypeface) {
                g_codeTypeface = g_typeface;
            }
        }

        return g_fontMgr != nullptr && g_typeface != nullptr && g_headingTypeface != nullptr && g_codeTypeface != nullptr;
    }

    bool IsHeading(mdviewer::BlockType blockType) {
        return blockType == mdviewer::BlockType::Heading1 ||
               blockType == mdviewer::BlockType::Heading2 ||
               blockType == mdviewer::BlockType::Heading3 ||
               blockType == mdviewer::BlockType::Heading4 ||
               blockType == mdviewer::BlockType::Heading5 ||
               blockType == mdviewer::BlockType::Heading6;
    }

    float GetBlockFontSize(mdviewer::BlockType blockType) {
        switch (blockType) {
            case mdviewer::BlockType::Heading1: return 34.0f;
            case mdviewer::BlockType::Heading2: return 28.0f;
            case mdviewer::BlockType::Heading3: return 22.0f;
            case mdviewer::BlockType::Heading4: return 19.0f;
            case mdviewer::BlockType::Heading5: return 17.0f;
            case mdviewer::BlockType::Heading6: return 16.0f;
            case mdviewer::BlockType::CodeBlock: return 15.0f;
            default: return 17.0f;
        }
    }

    const mdviewer::LineLayout* FindFirstLine(const mdviewer::BlockLayout& block) {
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

    void ConfigureFont(RenderContext& ctx, mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        const bool isCode = blockType == mdviewer::BlockType::CodeBlock || inlineStyle == mdviewer::InlineStyle::Code;
        const bool isHeading = IsHeading(blockType);
        ctx.font.setTypeface(isCode ? g_codeTypeface : (isHeading ? g_headingTypeface : g_typeface));
        ctx.font.setSize(isCode ? GetBlockFontSize(mdviewer::BlockType::CodeBlock) : GetBlockFontSize(blockType));
        ctx.font.setSubpixel(!isHeading);
        ctx.font.setHinting(SkFontHinting::kSlight);
        ctx.font.setEdging(isHeading ? SkFont::Edging::kAntiAlias : SkFont::Edging::kSubpixelAntiAlias);
        ctx.font.setEmbolden(inlineStyle == mdviewer::InlineStyle::Strong && !isHeading);
        ctx.font.setSkewX(inlineStyle == mdviewer::InlineStyle::Emphasis ? -0.18f : 0.0f);
        ctx.font.setScaleX(1.0f);
    }

    SkColor GetTextColor(mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        if (blockType == mdviewer::BlockType::Blockquote) {
            return SkColorSetRGB(86, 92, 105);
        }
        if (inlineStyle == mdviewer::InlineStyle::Code) {
            return SkColorSetRGB(165, 46, 84);
        }
        if (inlineStyle == mdviewer::InlineStyle::Link) {
            return SkColorSetRGB(26, 92, 200);
        }
        if (IsHeading(blockType)) {
            return SkColorSetRGB(28, 31, 38);
        }
        return SkColorSetRGB(36, 39, 45);
    }

    float GetContentX(const mdviewer::BlockLayout& block) {
        if (block.type == mdviewer::BlockType::CodeBlock) {
            return block.bounds.left() + kCodeBlockPaddingX;
        }
        if (block.type == mdviewer::BlockType::Blockquote) {
            return block.bounds.left() + kBlockquoteTextInset;
        }
        return block.bounds.left();
    }

    size_t GetSelectionStart() {
        return std::min(g_appState.selectionAnchor, g_appState.selectionFocus);
    }

    size_t GetSelectionEnd() {
        return std::max(g_appState.selectionAnchor, g_appState.selectionFocus);
    }

    float GetViewportHeight(HWND hwnd) {
        RECT rect;
        GetClientRect(hwnd, &rect);
        return static_cast<float>(rect.bottom - rect.top);
    }

    float GetMaxScroll(HWND hwnd) {
        return std::max(g_appState.docLayout.totalHeight - GetViewportHeight(hwnd), 0.0f);
    }

    void ClampScrollOffset(HWND hwnd) {
        g_appState.scrollOffset = std::clamp(g_appState.scrollOffset, 0.0f, GetMaxScroll(hwnd));
    }

    std::optional<SkRect> GetScrollbarThumbRect(HWND hwnd) {
        const float viewportHeight = GetViewportHeight(hwnd);
        if (g_appState.docLayout.totalHeight <= viewportHeight || viewportHeight <= 0.0f) {
            return std::nullopt;
        }

        const float scrollRatio = viewportHeight / g_appState.docLayout.totalHeight;
        const float thumbHeight = std::max(viewportHeight * scrollRatio, 24.0f);
        const float maxThumbTravel = viewportHeight - thumbHeight;
        const float maxScroll = GetMaxScroll(hwnd);
        const float thumbY = maxScroll > 0.0f ? (g_appState.scrollOffset / maxScroll) * maxThumbTravel : 0.0f;

        return SkRect::MakeXYWH(
            g_surface->width() - kScrollbarWidth - kScrollbarMargin,
            thumbY + kScrollbarMargin,
            kScrollbarWidth,
            thumbHeight);
    }

    size_t FindTextPositionInRun(RenderContext& ctx, mdviewer::BlockType blockType, const mdviewer::RunLayout& run, float xInRun) {
        ConfigureFont(ctx, blockType, run.style);

        if (xInRun <= 0.0f) {
            return run.textStart;
        }

        float bestDistance = std::numeric_limits<float>::max();
        size_t bestOffset = run.text.size();

        for (size_t offset = 0; offset <= run.text.size(); ) {
            const float width = ctx.font.measureText(run.text.c_str(), offset, SkTextEncoding::kUTF8);
            const float distance = std::abs(width - xInRun);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestOffset = offset;
            }

            if (offset == run.text.size()) {
                break;
            }

            offset++;
            while (offset < run.text.size() && (static_cast<unsigned char>(run.text[offset]) & 0xC0) == 0x80) {
                offset++;
            }
        }

        return run.textStart + bestOffset;
    }

    void DrawSelectionForLine(RenderContext& ctx, const mdviewer::BlockLayout& block, const mdviewer::LineLayout& line) {
        if (!g_appState.HasSelection()) {
            return;
        }

        const size_t selectionStart = GetSelectionStart();
        const size_t selectionEnd = GetSelectionEnd();
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            const size_t runStart = run.textStart;
            const size_t runEnd = run.textStart + run.text.size();

            if (selectionEnd <= runStart || selectionStart >= runEnd) {
                ConfigureFont(ctx, block.type, run.style);
                currentX += ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                continue;
            }

            ConfigureFont(ctx, block.type, run.style);
            const size_t highlightStart = std::max(selectionStart, runStart) - runStart;
            const size_t highlightEnd = std::min(selectionEnd, runEnd) - runStart;
            const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8);
            const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8);

            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(SkColorSetARGB(110, 102, 165, 255));
            ctx.canvas->drawRect(
                SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
                highlightPaint);

            currentX += ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
        }
    }

    bool FindBestHit(RenderContext& ctx,
                     const std::vector<mdviewer::BlockLayout>& blocks,
                     float x,
                     float documentY,
                     TextHit& bestHit,
                     float& bestDistance) {
        for (const auto& block : blocks) {
            for (const auto& line : block.lines) {
                const float lineTop = line.y;
                const float lineBottom = line.y + line.height;
                float distance = 0.0f;
                if (documentY < lineTop) {
                    distance = lineTop - documentY;
                } else if (documentY > lineBottom) {
                    distance = documentY - lineBottom;
                }

                if (distance > bestDistance) {
                    continue;
                }

                float currentX = GetContentX(block);
                size_t fallbackPosition = line.textStart;
                bool foundRun = false;

                for (const auto& run : line.runs) {
                    ConfigureFont(ctx, block.type, run.style);
                    const float runWidth = ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                    const float runEndX = currentX + runWidth;
                    fallbackPosition = run.textStart + run.text.size();

                    if (x <= runEndX || &run == &line.runs.back()) {
                        bestHit.position = FindTextPositionInRun(ctx, block.type, run, x - currentX);
                        bestHit.valid = true;
                        bestDistance = distance;
                        foundRun = true;
                        break;
                    }

                    currentX = runEndX;
                }

                if (!foundRun && line.runs.empty()) {
                    bestHit.position = line.textStart;
                    bestHit.valid = true;
                    bestDistance = distance;
                } else if (!foundRun && fallbackPosition >= line.textStart) {
                    bestHit.position = fallbackPosition;
                    bestHit.valid = true;
                    bestDistance = distance;
                }
            }

            if (FindBestHit(ctx, block.children, x, documentY, bestHit, bestDistance)) {
                // bestHit updated by recursion
            }
        }

        return bestHit.valid;
    }

    TextHit HitTestText(float x, float viewportY) {
        TextHit hit;
        if (!EnsureFontSystem()) {
            return hit;
        }

        RenderContext ctx;
        ctx.canvas = nullptr;
        ctx.paint.setAntiAlias(true);
        ctx.font.setTypeface(g_typeface);

        const float documentY = viewportY + g_appState.scrollOffset;
        float bestDistance = std::numeric_limits<float>::max();
        FindBestHit(ctx, g_appState.docLayout.blocks, x, documentY, hit, bestDistance);
        return hit;
    }

    bool CopyTextToClipboard(HWND hwnd, const std::string& utf8Text) {
        if (utf8Text.empty()) {
            return false;
        }

        const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), nullptr, 0);
        if (wideLength <= 0) {
            return false;
        }

        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLength + 1) * sizeof(wchar_t));
        if (!memory) {
            return false;
        }

        auto* wideText = static_cast<wchar_t*>(GlobalLock(memory));
        if (!wideText) {
            GlobalFree(memory);
            return false;
        }

        MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), wideText, wideLength);
        wideText[wideLength] = L'\0';
        GlobalUnlock(memory);

        if (!OpenClipboard(hwnd)) {
            GlobalFree(memory);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            CloseClipboard();
            GlobalFree(memory);
            return false;
        }

        CloseClipboard();
        return true;
    }

    void DrawBlockDecoration(RenderContext& ctx,
                             const mdviewer::BlockLayout& block,
                             mdviewer::BlockType parentType,
                             size_t siblingIndex) {
        if (block.type == mdviewer::BlockType::CodeBlock) {
            SkPaint backgroundPaint;
            backgroundPaint.setAntiAlias(true);
            backgroundPaint.setColor(SkColorSetRGB(245, 246, 248));
            ctx.canvas->drawRoundRect(
                SkRect::MakeLTRB(
                    block.bounds.left() - kCodeBlockPaddingX,
                    block.bounds.top() - kCodeBlockPaddingY,
                    block.bounds.right() + kCodeBlockPaddingX,
                    block.bounds.bottom() + kCodeBlockPaddingY),
                10.0f,
                10.0f,
                backgroundPaint);
        } else if (block.type == mdviewer::BlockType::Blockquote) {
            SkPaint accentPaint;
            accentPaint.setAntiAlias(true);
            accentPaint.setColor(SkColorSetRGB(196, 204, 217));
            ctx.canvas->drawRoundRect(
                SkRect::MakeXYWH(
                    block.bounds.left(),
                    block.bounds.top(),
                    kBlockquoteAccentWidth,
                    std::max(block.bounds.height(), 12.0f)),
                2.0f,
                2.0f,
                accentPaint);
        } else if (block.type == mdviewer::BlockType::ListItem &&
                   (parentType == mdviewer::BlockType::UnorderedList || parentType == mdviewer::BlockType::OrderedList)) {
            const mdviewer::LineLayout* firstLine = FindFirstLine(block);
            if (!firstLine) {
                return;
            }

            ConfigureFont(ctx, mdviewer::BlockType::Paragraph, mdviewer::InlineStyle::Plain);
            ctx.paint.setColor(SkColorSetRGB(90, 96, 110));
            const float markerBaseline = firstLine->y + firstLine->height - kTextBaselineOffset;
            const float markerX = block.bounds.left() - kListMarkerGap;

            if (parentType == mdviewer::BlockType::OrderedList) {
                const std::string marker = std::to_string(siblingIndex + 1) + ".";
                ctx.canvas->drawString(marker.c_str(), markerX - 6.0f, markerBaseline, ctx.font, ctx.paint);
            } else {
                ctx.canvas->drawCircle(markerX, markerBaseline - (firstLine->height * 0.35f), 3.0f, ctx.paint);
            }
        }
    }

    void DrawLine(RenderContext& ctx, const mdviewer::BlockLayout& block, const mdviewer::LineLayout& line) {
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            ConfigureFont(ctx, block.type, run.style);

            SkRect textBounds;
            const float advance = ctx.font.measureText(
                run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8, &textBounds);
            const float baselineY = std::round(line.y + line.height - kTextBaselineOffset);

            if (run.style == mdviewer::InlineStyle::Code && !run.text.empty()) {
                SkPaint chipPaint;
                chipPaint.setAntiAlias(true);
                chipPaint.setColor(SkColorSetRGB(241, 243, 245));
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

            ctx.paint.setColor(GetTextColor(block.type, run.style));
            ctx.canvas->drawString(run.text.c_str(), currentX, baselineY, ctx.font, ctx.paint);

            if (run.style == mdviewer::InlineStyle::Link && advance > 0.0f) {
                SkPaint underlinePaint;
                underlinePaint.setAntiAlias(true);
                underlinePaint.setStrokeWidth(1.0f);
                underlinePaint.setColor(GetTextColor(block.type, run.style));
                ctx.canvas->drawLine(currentX, baselineY + 2.0f, currentX + advance, baselineY + 2.0f, underlinePaint);
            }

            currentX += advance;
        }
    }
}

void DrawBlocks(RenderContext& ctx,
                const std::vector<mdviewer::BlockLayout>& blocks,
                mdviewer::BlockType parentType = mdviewer::BlockType::Paragraph) {
    for (size_t index = 0; index < blocks.size(); ++index) {
        const auto& block = blocks[index];
        if (block.type == mdviewer::BlockType::ThematicBreak) {
            ctx.paint.setColor(SkColorSetRGB(210, 214, 220));
            ctx.paint.setStrokeWidth(1.0f);
            ctx.canvas->drawLine(block.bounds.left(), block.bounds.centerY(), block.bounds.right(), block.bounds.centerY(), ctx.paint);
        } else {
            DrawBlockDecoration(ctx, block, parentType, index);

            for (const auto& line : block.lines) {
                DrawSelectionForLine(ctx, block, line);
                DrawLine(ctx, block, line);
            }

            if (!block.children.empty()) {
                DrawBlocks(ctx, block.children, block.type);
            }
        }
    }
}

void UpdateSurface(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) return;

    SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
    g_surface = SkSurfaces::Raster(info);
}

void Render(HWND hwnd) {
    if (!g_surface) return;

    if (!EnsureFontSystem()) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    SkCanvas* canvas = g_surface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        
        RenderContext ctx;
        ctx.canvas = canvas;
        ctx.paint.setAntiAlias(true);
        ctx.font.setTypeface(g_typeface);
        ctx.font.setSubpixel(true);
        ctx.font.setHinting(SkFontHinting::kSlight);
        ctx.font.setEdging(SkFont::Edging::kSubpixelAntiAlias);

        canvas->save();
        canvas->translate(0, -g_appState.scrollOffset);

        DrawBlocks(ctx, g_appState.docLayout.blocks);

        if (g_appState.sourceText.empty()) {
            ctx.font.setSize(20.0f);
            ctx.paint.setColor(SK_ColorGRAY);
            const char* msg = "Drag and drop a Markdown file here";
            SkRect bounds;
            ctx.font.measureText(msg, strlen(msg), SkTextEncoding::kUTF8, &bounds);
            canvas->drawString(msg, (g_surface->width() - bounds.width()) / 2, g_surface->height() / 2, ctx.font, ctx.paint);
        }

        canvas->restore();

        // Draw simple scrollbar indicator
        if (const auto thumb = GetScrollbarThumbRect(hwnd)) {
            SkPaint trackPaint;
            trackPaint.setAntiAlias(true);
            trackPaint.setColor(SkColorSetARGB(24, 0, 0, 0));
            ctx.canvas->drawRoundRect(
                SkRect::MakeXYWH(
                    g_surface->width() - kScrollbarWidth - kScrollbarMargin,
                    kScrollbarMargin,
                    kScrollbarWidth,
                    std::max(GetViewportHeight(hwnd) - (kScrollbarMargin * 2.0f), 1.0f)),
                5.0f,
                5.0f,
                trackPaint);

            ctx.paint.setColor(SkColorSetARGB(120, 100, 100, 100));
            ctx.canvas->drawRoundRect(*thumb, 5.0f, 5.0f, ctx.paint);
        }
    }

    SkPixmap pixmap;
    if (g_surface->peekPixels(&pixmap)) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pixmap.width();
        bmi.bmiHeader.biHeight = -pixmap.height();
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(hdc, 0, 0, pixmap.width(), pixmap.height(),
                      0, 0, pixmap.width(), pixmap.height(),
                      pixmap.addr(), &bmi, DIB_RGB_COLORS, SRCCOPY);

        EndPaint(hwnd, &ps);
    } else {
        // Fallback Begin/EndPaint to clear update region if peekPixels fails
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
    }
}

void LoadFile(HWND hwnd, const std::filesystem::path& path) {
    if (!EnsureFontSystem()) {
        MessageBoxW(hwnd, L"Font initialization failed. The document cannot be rendered.", L"Error", MB_ICONERROR);
        return;
    }

    auto content = mdviewer::ReadFileToString(path);
    if (content) {
        auto docModel = mdviewer::MarkdownParser::Parse(*content);
        
        RECT rect;
        GetClientRect(hwnd, &rect);
        float width = static_cast<float>(rect.right - rect.left);
        auto layout = mdviewer::LayoutEngine::ComputeLayout(docModel, width, g_typeface.get());

        g_appState.SetFile(path, std::move(*content), std::move(docModel), std::move(layout));
        
        std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
        SetWindowTextW(hwnd, title.c_str());

        InvalidateRect(hwnd, NULL, FALSE);
    } else {
        MessageBoxW(hwnd, L"Could not load file.", L"Error", MB_ICONERROR);
    }
}

void OnDropFiles(HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (count > 0) {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            LoadFile(hwnd, path);
        }
    }
    DragFinish(hDrop);
}

void UpdateSelectionFromPoint(HWND hwnd, int x, int y) {
    auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));
    if (hit.valid) {
        g_appState.selectionFocus = hit.position;
    }

    const float viewportHeight = GetViewportHeight(hwnd);
    if (y < 0) {
        g_appState.scrollOffset = std::max(g_appState.scrollOffset + static_cast<float>(y), 0.0f);
    } else if (y > viewportHeight) {
        g_appState.scrollOffset = std::min(g_appState.scrollOffset + static_cast<float>(y - viewportHeight), GetMaxScroll(hwnd));
    }
}

void UpdateScrollOffsetFromThumb(HWND hwnd, int mouseY) {
    const auto thumb = GetScrollbarThumbRect(hwnd);
    if (!thumb) {
        return;
    }

    const float viewportHeight = GetViewportHeight(hwnd);
    const float thumbHeight = thumb->height();
    const float trackHeight = viewportHeight - thumbHeight - (kScrollbarMargin * 2.0f);
    const float thumbTop = std::clamp(
        static_cast<float>(mouseY) - g_appState.scrollbarDragOffset,
        kScrollbarMargin,
        kScrollbarMargin + std::max(trackHeight, 0.0f));

    const float maxScroll = GetMaxScroll(hwnd);
    if (maxScroll <= 0.0f) {
        g_appState.scrollOffset = 0.0f;
        return;
    }

    const float normalized = trackHeight > 0.0f ? (thumbTop - kScrollbarMargin) / trackHeight : 0.0f;
    g_appState.scrollOffset = normalized * maxScroll;
}

bool CopySelection(HWND hwnd) {
    if (!g_appState.HasSelection()) {
        return false;
    }

    const size_t selectionStart = GetSelectionStart();
    const size_t selectionEnd = GetSelectionEnd();
    if (selectionEnd > g_appState.docLayout.plainText.size() || selectionStart >= selectionEnd) {
        return false;
    }

    return CopyTextToClipboard(hwnd, g_appState.docLayout.plainText.substr(selectionStart, selectionEnd - selectionStart));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            UpdateSurface(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            Render(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES:
            OnDropFiles(hwnd, (HDROP)wParam);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            SetCapture(hwnd);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (const auto thumb = GetScrollbarThumbRect(hwnd); thumb && thumb->contains(static_cast<float>(x), static_cast<float>(y))) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = static_cast<float>(y) - thumb->top();
            } else if (x >= g_surface->width() - static_cast<int>(kScrollbarWidth + (kScrollbarMargin * 2.0f)) && GetScrollbarThumbRect(hwnd)) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = GetScrollbarThumbRect(hwnd)->height() * 0.5f;
                UpdateScrollOffsetFromThumb(hwnd, y);
            } else {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isSelecting = true;
                g_appState.isDraggingScrollbar = false;
                auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));
                if (hit.valid) {
                    g_appState.selectionAnchor = hit.position;
                    g_appState.selectionFocus = hit.position;
                } else {
                    g_appState.selectionAnchor = 0;
                    g_appState.selectionFocus = 0;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if ((wParam & MK_LBUTTON) == 0) {
                return 0;
            }

            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.isDraggingScrollbar) {
                UpdateScrollOffsetFromThumb(hwnd, GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_appState.isSelecting) {
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isDraggingScrollbar = false;
            if (g_appState.isSelecting) {
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            g_appState.isSelecting = false;
            if (!g_appState.HasSelection()) {
                g_appState.selectionAnchor = 0;
                g_appState.selectionFocus = 0;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.scrollOffset -= static_cast<float>(delta);
                ClampScrollOffset(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                CopySelection(hwnd);
                return 0;
            }
            break;
        case WM_SIZE: {
            UpdateSurface(hwnd);
            if (!g_appState.sourceText.empty()) {
                float width = static_cast<float>(LOWORD(lParam));
                auto layout = mdviewer::LayoutEngine::ComputeLayout(g_appState.docModel, width, g_typeface.get());
                {
                    std::lock_guard<std::mutex> lock(g_appState.mtx);
                    g_appState.docLayout = std::move(layout);
                    ClampScrollOffset(hwnd);
                    g_appState.needsRepaint = true;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInitResult);
    if (FAILED(comInitResult) && comInitResult != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"COM initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        return 1;
    }

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Markdown Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 0;
    }

    if (!EnsureFontSystem()) {
        MessageBoxW(hwnd, L"Font initialization failed. The application cannot render text.", L"Error", MB_ICONERROR);
        DestroyWindow(hwnd);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            LoadFile(hwnd, argv[1]);
        }
        LocalFree(argv);
    }

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return 0;
}
