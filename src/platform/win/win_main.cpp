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
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <vector>

#include "app/app_config.h"
#include "app/app_state.h"
#include "app/document_loader.h"
#include "app/link_resolver.h"
#include "app/viewer_controller.h"
#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_surface.h"
#include "render/theme.h"
#include "render/typography.h"
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
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSamplingOptions.h"
#pragma warning(pop)

void AdjustBaseFontSize(HWND hwnd, float delta);

namespace {
    mdviewer::AppState g_appState;
    sk_sp<SkSurface> g_surface;
    sk_sp<SkTypeface> g_typeface;
    sk_sp<SkTypeface> g_boldTypeface;
    sk_sp<SkTypeface> g_headingTypeface;
    sk_sp<SkTypeface> g_codeTypeface;
    sk_sp<SkFontMgr> g_fontMgr;
    std::wstring g_selectedFontFamily;
    std::filesystem::path g_configPath;

    void OnGoBack(HWND hwnd);
    void OnGoForward(HWND hwnd);
    bool LoadFile(HWND hwnd, const std::filesystem::path& path, bool pushHistory = true);

    constexpr float kTextBaselineOffset = 5.0f;
    constexpr float kCodeBlockPaddingX = 8.0f;
    constexpr float kCodeBlockPaddingY = 8.0f;
    constexpr float kCodeBlockMarginY = 16.0f;
    constexpr float kBlockquoteAccentWidth = 4.0f;
    constexpr float kBlockquoteTextInset = 18.0f;
    constexpr float kListMarkerGap = 16.0f;
    constexpr float kScrollbarWidth = 10.0f;
    constexpr float kScrollbarMargin = 4.0f;
    constexpr float kAutoScrollDeadZone = 2.0f;
    constexpr UINT_PTR kAutoScrollTimerId = 2001;
    constexpr UINT_PTR kCopiedFeedbackTimerId = 2002;
    constexpr UINT kAutoScrollTimerMs = 16;
    constexpr int kLinkClickSlop = 4;
    constexpr int kInitialWindowWidth = 900;
    constexpr int kInitialWindowHeight = 1200;
    constexpr int kAppIconResourceId = 101;

    struct RenderContext {
        SkCanvas* canvas;
        SkPaint paint;
        SkFont font;
    };

    struct TextHit {
        size_t position = 0;
        bool valid = false;
        std::string url;
        mdviewer::InlineStyle style = mdviewer::InlineStyle::Plain;
    };

    std::map<std::string, sk_sp<SkImage>> g_imageCache;

    mdviewer::ThemeMode GetCurrentThemeMode() {
        return g_appState.theme;
    }

    mdviewer::ThemePalette GetCurrentThemePalette() {
        return GetThemePalette(GetCurrentThemeMode());
    }

    void SyncMenuState(HWND hwnd) {
        mdviewer::win::UpdateMenuState(
            hwnd,
            g_appState.theme,
            !g_selectedFontFamily.empty(),
            GetCurrentThemePalette());
    }

    bool EnsureFontSystem();

    float GetContentTopInset() {
        return static_cast<float>(mdviewer::win::kMenuBarHeight);
    }

    std::optional<std::filesystem::path> ShowOpenFileDialog(HWND hwnd) {
        wchar_t fileBuffer[MAX_PATH] = {};
        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter =
            L"Markdown Files (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.txt\0"
            L"All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        dialog.lpstrDefExt = L"md";

        if (!GetOpenFileNameW(&dialog)) {
            return std::nullopt;
        }

        return std::filesystem::path(fileBuffer);
    }

    std::string WideToUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) {
            return {};
        }

        std::string utf8(static_cast<size_t>(utf8Length), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), utf8Length, nullptr, nullptr);
        return utf8;
    }

    std::wstring Utf8ToWide(const std::string& text) {
        if (text.empty()) {
            return {};
        }

        const int wideLength = MultiByteToWideChar(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        if (wideLength <= 0) {
            return {};
        }

        std::wstring wide(static_cast<size_t>(wideLength), L'\0');
        MultiByteToWideChar(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), wideLength);
        return wide;
    }

    std::filesystem::path GetExecutableConfigPath() {
        std::array<wchar_t, MAX_PATH> buffer = {};
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::filesystem::path(L"mdviewer.ini");
        }

        std::filesystem::path executablePath(std::wstring(buffer.data(), length));
        return executablePath.parent_path() / L"mdviewer.ini";
    }

    void SaveCurrentConfig() {
        if (g_configPath.empty()) {
            return;
        }

        mdviewer::AppConfig config;
        config.theme = g_appState.theme;
        config.fontFamilyUtf8 = WideToUtf8(g_selectedFontFamily);
        config.baseFontSize = g_appState.baseFontSize;
        mdviewer::SaveAppConfig(g_configPath, config);
    }

    void LoadInitialConfig() {
        g_configPath = GetExecutableConfigPath();

        const auto config = mdviewer::LoadAppConfig(g_configPath);
        if (!config) {
            g_appState.theme = mdviewer::ThemeMode::Light;
            g_appState.baseFontSize = mdviewer::kDefaultBaseFontSize;
            g_selectedFontFamily.clear();
            return;
        }

        g_appState.theme = config->theme;
        g_appState.baseFontSize = mdviewer::ClampBaseFontSize(config->baseFontSize);
        g_selectedFontFamily = Utf8ToWide(config->fontFamilyUtf8);
    }

    void ResetResolvedTypefaces() {
        g_typeface.reset();
        g_boldTypeface.reset();
        g_headingTypeface.reset();
        g_codeTypeface.reset();
    }

    sk_sp<SkTypeface> CreateDocumentTypeface(SkFontStyle style) {
        if (!g_fontMgr) {
            return nullptr;
        }

        if (!g_selectedFontFamily.empty()) {
            const std::string utf8Family = WideToUtf8(g_selectedFontFamily);
            if (auto typeface = mdviewer::CreateStyledTypeface(g_fontMgr, utf8Family.c_str(), style)) {
                return typeface;
            }
        }

        return mdviewer::CreateDefaultTypeface(g_fontMgr, style);
    }

    std::optional<std::wstring> ShowFontDialog(HWND hwnd) {
        LOGFONTW logFont = {};
        if (!g_selectedFontFamily.empty()) {
            lstrcpynW(logFont.lfFaceName, g_selectedFontFamily.c_str(), LF_FACESIZE);
        }

        CHOOSEFONTW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpLogFont = &logFont;
        dialog.Flags = CF_SCREENFONTS | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOSIZESEL;

        if (!ChooseFontW(&dialog)) {
            return std::nullopt;
        }

        return std::wstring(logFont.lfFaceName);
    }

    bool CanZoomIn() {
        return g_appState.baseFontSize < mdviewer::ClampBaseFontSize(g_appState.baseFontSize + 1.0f);
    }

    bool CanZoomOut() {
        return g_appState.baseFontSize > mdviewer::ClampBaseFontSize(g_appState.baseFontSize - 1.0f);
    }

    bool EnsureFontSystem() {
        if (!g_fontMgr) {
            g_fontMgr = mdviewer::CreateFontManager();
        }

        if (!g_typeface) {
            g_typeface = CreateDocumentTypeface(SkFontStyle::Normal());
        }

        if (!g_boldTypeface) {
            g_boldTypeface = CreateDocumentTypeface(SkFontStyle::Bold());
            if (!g_boldTypeface) {
                g_boldTypeface = g_typeface;
            }
        }

        if (!g_headingTypeface) {
            g_headingTypeface = g_boldTypeface;
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

        return g_fontMgr != nullptr &&
               g_typeface != nullptr &&
               g_boldTypeface != nullptr &&
               g_headingTypeface != nullptr &&
               g_codeTypeface != nullptr;
    }

    bool IsHeading(mdviewer::BlockType blockType) {
        return blockType == mdviewer::BlockType::Heading1 ||
               blockType == mdviewer::BlockType::Heading2 ||
               blockType == mdviewer::BlockType::Heading3 ||
               blockType == mdviewer::BlockType::Heading4 ||
               blockType == mdviewer::BlockType::Heading5 ||
               blockType == mdviewer::BlockType::Heading6;
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
        const bool isStrong = inlineStyle == mdviewer::InlineStyle::Strong;
        ctx.font.setTypeface(
            isCode ? g_codeTypeface : (isHeading ? g_headingTypeface : (isStrong ? g_boldTypeface : g_typeface)));
        ctx.font.setSize(
            isCode
                ? mdviewer::GetBlockFontSize(mdviewer::BlockType::CodeBlock, g_appState.baseFontSize)
                : mdviewer::GetBlockFontSize(blockType, g_appState.baseFontSize));
        ctx.font.setSubpixel(!isHeading);
        ctx.font.setHinting(SkFontHinting::kSlight);
        ctx.font.setEdging(isHeading ? SkFont::Edging::kAntiAlias : SkFont::Edging::kSubpixelAntiAlias);
        ctx.font.setEmbolden(false);
        ctx.font.setSkewX(inlineStyle == mdviewer::InlineStyle::Emphasis ? -0.18f : 0.0f);
        ctx.font.setScaleX(1.0f);
    }

    SkColor GetTextColor(mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        const mdviewer::ThemePalette palette = GetCurrentThemePalette();
        if (blockType == mdviewer::BlockType::Blockquote) {
            return palette.blockquoteText;
        }
        if (inlineStyle == mdviewer::InlineStyle::Code) {
            return palette.codeText;
        }
        if (inlineStyle == mdviewer::InlineStyle::Link) {
            return palette.linkText;
        }
        if (IsHeading(blockType)) {
            return palette.headingText;
        }
        return palette.bodyText;
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
        return std::max(static_cast<float>(rect.bottom - rect.top) - GetContentTopInset(), 0.0f);
    }

    float GetMaxScroll(HWND hwnd) {
        return std::max(g_appState.docLayout.totalHeight - GetViewportHeight(hwnd), 0.0f);
    }

    void ClampScrollOffset(HWND hwnd) {
        g_appState.scrollOffset = std::clamp(g_appState.scrollOffset, 0.0f, GetMaxScroll(hwnd));
    }

    void StopAutoScroll(HWND hwnd) {
        const bool shouldReleaseCapture = GetCapture() == hwnd;
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isAutoScrolling = false;
            g_appState.autoScrollOriginX = 0.0f;
            g_appState.autoScrollOriginY = 0.0f;
            g_appState.autoScrollCursorX = 0.0f;
            g_appState.autoScrollCursorY = 0.0f;
        }
        KillTimer(hwnd, kAutoScrollTimerId);
        if (shouldReleaseCapture) {
            ReleaseCapture();
        }
    }

    void StartAutoScroll(HWND hwnd, int x, int y) {
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isAutoScrolling = true;
            g_appState.isSelecting = false;
            g_appState.isDraggingScrollbar = false;
            g_appState.autoScrollOriginX = static_cast<float>(x);
            g_appState.autoScrollOriginY = static_cast<float>(y);
            g_appState.autoScrollCursorX = static_cast<float>(x);
            g_appState.autoScrollCursorY = static_cast<float>(y);
        }
        SetCapture(hwnd);
        SetTimer(hwnd, kAutoScrollTimerId, kAutoScrollTimerMs, nullptr);
    }

    float ComputeAutoScrollVelocity(float delta) {
        const float magnitude = std::abs(delta);
        if (magnitude <= kAutoScrollDeadZone) {
            return 0.0f;
        }

        const float adjusted = magnitude - kAutoScrollDeadZone;
        const float normalized = std::min(adjusted / 16.0f, 25.0f);
        const float speed = (normalized * normalized) * 0.55f + (adjusted * 0.22f);
        return delta < 0.0f ? -speed : speed;
    }

    bool TickAutoScroll(HWND hwnd) {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        if (!g_appState.isAutoScrolling) {
            return false;
        }

        const float deltaY = g_appState.autoScrollCursorY - g_appState.autoScrollOriginY;
        const float scrollDelta = ComputeAutoScrollVelocity(deltaY);
        if (scrollDelta == 0.0f) {
            return false;
        }

        const float previousOffset = g_appState.scrollOffset;
        g_appState.scrollOffset = std::clamp(g_appState.scrollOffset + scrollDelta, 0.0f, GetMaxScroll(hwnd));
        return std::abs(g_appState.scrollOffset - previousOffset) > 0.01f;
    }

    void DrawAutoScrollIndicator(SkCanvas* canvas) {
        const mdviewer::ThemePalette palette = GetCurrentThemePalette();
        if (!g_appState.isAutoScrolling) {
            return;
        }
        const float originX = g_appState.autoScrollOriginX;
        const float originY = g_appState.autoScrollOriginY;

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
            thumbY + kScrollbarMargin + GetContentTopInset(),
            kScrollbarWidth,
            thumbHeight);
    }

    size_t GetRunTextEnd(const mdviewer::RunLayout& run) {
        if (run.style == mdviewer::InlineStyle::Image) {
            return run.textStart;
        }
        return run.textStart + run.text.size();
    }

    float GetRunVisualWidth(RenderContext& ctx, mdviewer::BlockType blockType, const mdviewer::RunLayout& run) {
        if (run.style == mdviewer::InlineStyle::Image) {
            return run.imageWidth;
        }
        ConfigureFont(ctx, blockType, run.style);
        return ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
    }

    size_t FindTextPositionInRun(RenderContext& ctx, mdviewer::BlockType blockType, const mdviewer::RunLayout& run, float xInRun) {
        if (run.style == mdviewer::InlineStyle::Image) {
            return run.textStart;
        }

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

        const mdviewer::ThemePalette palette = GetCurrentThemePalette();

        const size_t selectionStart = GetSelectionStart();
        const size_t selectionEnd = GetSelectionEnd();
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            const size_t runStart = run.textStart;
            const size_t runEnd = GetRunTextEnd(run);
            const float runWidth = GetRunVisualWidth(ctx, block.type, run);

            if (selectionEnd <= runStart || selectionStart >= runEnd) {
                currentX += runWidth;
                continue;
            }

            ConfigureFont(ctx, block.type, run.style);
            const size_t highlightStart = std::max(selectionStart, runStart) - runStart;
            const size_t highlightEnd = std::min(selectionEnd, runEnd) - runStart;
            const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8);
            const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8);

            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(palette.selectionFill);
            ctx.canvas->drawRect(
                SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
                highlightPaint);

            currentX += runWidth;
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
                    const float runWidth = GetRunVisualWidth(ctx, block.type, run);
                    const float runEndX = currentX + runWidth;
                    fallbackPosition = GetRunTextEnd(run);

                    if (x <= runEndX || &run == &line.runs.back()) {
                        bestHit.position = FindTextPositionInRun(ctx, block.type, run, x - currentX);
                        bestHit.valid = true;
                        bestHit.url = run.url;
                        bestHit.style = run.style;
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
        viewportY -= GetContentTopInset();
        if (viewportY < 0.0f) {
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

    void DrawCopyIcon(SkCanvas* canvas, float x, float y, float size, SkColor color) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(color);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(1.5f);
        paint.setStrokeCap(SkPaint::kRound_Cap);
        paint.setStrokeJoin(SkPaint::kRound_Join);

        // Main clipboard body
        SkRect body = SkRect::MakeXYWH(x + size * 0.2f, y + size * 0.3f, size * 0.6f, size * 0.6f);
        canvas->drawRoundRect(body, 2.0f, 2.0f, paint);

        // Clipboard top clip
        SkRect clip = SkRect::MakeXYWH(x + size * 0.35f, y + size * 0.15f, size * 0.3f, size * 0.25f);
        canvas->drawRoundRect(clip, 1.0f, 1.0f, paint);
    }

    void DrawBlockDecoration(RenderContext& ctx,
                             const mdviewer::BlockLayout& block,
                             mdviewer::BlockType parentType,
                             size_t siblingIndex) {
        const mdviewer::ThemePalette palette = GetCurrentThemePalette();

        if (block.type == mdviewer::BlockType::CodeBlock) {
            SkPaint backgroundPaint;
            backgroundPaint.setAntiAlias(true);
            backgroundPaint.setColor(palette.codeBlockBackground);
            
            // Draw background flush with left edge
            SkRect bgRect = SkRect::MakeLTRB(
                block.bounds.left(),
                block.bounds.top() - kCodeBlockPaddingY,
                block.bounds.right() + kCodeBlockPaddingX,
                block.bounds.bottom() + kCodeBlockPaddingY);
            
            ctx.canvas->drawRoundRect(bgRect, 8.0f, 8.0f, backgroundPaint);

            // Draw copy button
            const float btnSize = 28.0f;
            const float btnPadding = 6.0f;
            SkRect btnRect = SkRect::MakeXYWH(
                bgRect.right() - btnSize - btnPadding,
                bgRect.top() + btnPadding,
                btnSize,
                btnSize);
            
            // Store for hit testing (document coordinates)
            g_appState.codeBlockButtons.push_back({btnRect, {block.textStart, block.textStart + block.textLength}});

            DrawCopyIcon(ctx.canvas, btnRect.left(), btnRect.top(), btnSize, palette.listMarker);
        } else if (block.type == mdviewer::BlockType::Blockquote) {
            SkPaint accentPaint;
            accentPaint.setAntiAlias(true);
            accentPaint.setColor(palette.blockquoteAccent);
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
            ctx.paint.setColor(palette.listMarker);
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
        const mdviewer::ThemePalette palette = GetCurrentThemePalette();
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            ConfigureFont(ctx, block.type, run.style);

            SkRect textBounds;
            const float advance = ctx.font.measureText(
                run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8, &textBounds);
            const float baselineY = std::round(line.y + line.height - kTextBaselineOffset);

            if (run.style == mdviewer::InlineStyle::Image && !run.url.empty()) {
                sk_sp<SkImage> image;
                auto it = g_imageCache.find(run.url);
                if (it != g_imageCache.end()) {
                    image = it->second;
                } else {
                    // Try to load image
                    std::filesystem::path imagePath = run.url;
                    if (imagePath.is_relative()) {
                        imagePath = g_appState.currentFilePath.parent_path() / run.url;
                    }
                    
                    if (std::filesystem::exists(imagePath)) {
                        auto data = SkData::MakeFromFileName(imagePath.string().c_str());
                        if (data) {
                            image = SkImages::DeferredFromEncodedData(data);
                            if (image) {
                                g_imageCache[run.url] = image;
                                // After loading, we might want to relayout if actual size is different,
                                // but for now we used a placeholder size.
                            }
                        }
                    }
                }

                float displayW = run.imageWidth;
                float displayH = run.imageHeight;
                
                // If it's a large block image (scaled to ~90% width), center it
                float drawX = currentX;
                const float blockW = block.bounds.width();
                if (displayW > blockW * 0.8f) {
                    drawX = block.bounds.left() + (blockW - displayW) * 0.5f;
                }

                if (image) {
                    ctx.canvas->drawImageRect(image, 
                        SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH),
                        SkSamplingOptions(SkFilterMode::kLinear));
                } else {
                    // Draw placeholder
                    SkPaint placeholderPaint;
                    placeholderPaint.setStyle(SkPaint::kStroke_Style);
                    placeholderPaint.setColor(palette.listMarker);
                    placeholderPaint.setStrokeWidth(1.0f);
                    SkRect rect = SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH);
                    ctx.canvas->drawRect(rect, placeholderPaint);
                    ctx.canvas->drawLine(rect.left(), rect.top(), rect.right(), rect.bottom(), placeholderPaint);
                    ctx.canvas->drawLine(rect.right(), rect.top(), rect.left(), rect.bottom(), placeholderPaint);
                }
                currentX += displayW + 4.0f;
                continue;
            }

            if (run.style == mdviewer::InlineStyle::Code && !run.text.empty()) {
                SkPaint chipPaint;
                chipPaint.setAntiAlias(true);
                chipPaint.setColor(palette.codeInlineBackground);
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
    void PreloadImage(const std::string& url, const std::filesystem::path& baseDir) {
        if (g_imageCache.find(url) != g_imageCache.end()) {
            return;
        }

        std::filesystem::path imagePath = url;
        if (imagePath.is_relative()) {
            imagePath = baseDir / url;
        }

        if (std::filesystem::exists(imagePath)) {
            auto data = SkData::MakeFromFileName(imagePath.string().c_str());
            if (data) {
                auto image = SkImages::DeferredFromEncodedData(data);
                if (image) {
                    g_imageCache[url] = image;
                }
            }
        }
    }

    void PreloadImages(const mdviewer::DocumentModel& doc, const std::filesystem::path& baseDir) {
        for (const auto& block : doc.blocks) {
            for (const auto& run : block.inlineRuns) {
                if (run.style == mdviewer::InlineStyle::Image) {
                    PreloadImage(run.url, baseDir);
                }
            }
            // For nested blocks (like lists or blockquotes)
            if (!block.children.empty()) {
                mdviewer::DocumentModel subDoc;
                subDoc.blocks = block.children;
                PreloadImages(subDoc, baseDir);
            }
            }
            }

            void DrawBlocks(RenderContext& ctx,
                    const std::vector<mdviewer::BlockLayout>& blocks,
                    mdviewer::BlockType parentType = mdviewer::BlockType::Paragraph) {
            for (size_t index = 0; index < blocks.size(); ++index) {
            const auto& block = blocks[index];
            if (block.type == mdviewer::BlockType::ThematicBreak) {
                ctx.paint.setColor(GetCurrentThemePalette().thematicBreak);
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
            mdviewer::win::EnsureRasterSurfaceSize(hwnd, g_surface);
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
            const mdviewer::ThemePalette palette = GetCurrentThemePalette();
            canvas->clear(palette.windowBackground);

            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.codeBlockButtons.clear();

                RenderContext ctx;            ctx.canvas = canvas;
            ctx.paint.setAntiAlias(true);
            ctx.font.setTypeface(g_typeface);
            ctx.font.setSubpixel(true);
            ctx.font.setHinting(SkFontHinting::kSlight);
            ctx.font.setEdging(SkFont::Edging::kSubpixelAntiAlias);

            canvas->save();
            canvas->clipRect(SkRect::MakeLTRB(
                0.0f,
                GetContentTopInset(),
                static_cast<float>(g_surface->width()),
                static_cast<float>(g_surface->height())));
            canvas->translate(0, GetContentTopInset() - g_appState.scrollOffset);

            DrawBlocks(ctx, g_appState.docLayout.blocks);

            if (g_appState.sourceText.empty()) {
                ctx.font.setSize(mdviewer::GetEmptyStateFontSize(g_appState.baseFontSize));
                ctx.paint.setColor(palette.emptyStateText);
                const char* msg = "Drag and drop a Markdown file here";
                SkRect bounds;
                ctx.font.measureText(msg, strlen(msg), SkTextEncoding::kUTF8, &bounds);
                const float emptyStateY = GetViewportHeight(hwnd) * 0.5f;
                canvas->drawString(msg, (g_surface->width() - bounds.width()) / 2, emptyStateY, ctx.font, ctx.paint);
            }

            canvas->restore();

            mdviewer::win::DrawTopMenuBar(
                canvas,
                hwnd,
                g_surface->width(),
                g_typeface.get(),
                palette,
                g_appState.CanGoBack(),
                g_appState.CanGoForward(),
                CanZoomOut(),
                CanZoomIn());

            // Draw simple scrollbar indicator
            if (const auto thumb = GetScrollbarThumbRect(hwnd)) {
                SkPaint trackPaint;
                trackPaint.setAntiAlias(true);
                trackPaint.setColor(palette.scrollbarTrack);
                ctx.canvas->drawRoundRect(
                    SkRect::MakeXYWH(
                        g_surface->width() - kScrollbarWidth - kScrollbarMargin,
                        kScrollbarMargin + GetContentTopInset(),
                        kScrollbarWidth,
                        std::max(GetViewportHeight(hwnd) - (kScrollbarMargin * 2.0f), 1.0f)),
                    5.0f,
                    5.0f,
                    trackPaint);

                ctx.paint.setColor(palette.scrollbarThumb);
                ctx.canvas->drawRoundRect(*thumb, 5.0f, 5.0f, ctx.paint);
            }

            DrawAutoScrollIndicator(ctx.canvas);

            // Draw hovered URL overlay at the bottom
            if (!g_appState.hoveredUrl.empty()) {
                const float padding = 6.0f;
                ctx.font.setSize(mdviewer::GetHoverOverlayFontSize(g_appState.baseFontSize));
                ctx.font.setTypeface(g_typeface);

                SkRect textBounds;
                ctx.font.measureText(g_appState.hoveredUrl.c_str(), g_appState.hoveredUrl.size(), SkTextEncoding::kUTF8, &textBounds);

                const float overlayW = textBounds.width() + (padding * 2.0f);
                const float overlayH = 24.0f;
                const float overlayX = 10.0f;
                const float overlayY = static_cast<float>(g_surface->height()) - overlayH - 10.0f;

                SkPaint bPaint;
                bPaint.setAntiAlias(true);
                bPaint.setColor(palette.menuBackground);
                bPaint.setAlphaf(0.9f);
                ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 4.0f, 4.0f, bPaint);

                SkPaint borderPaint;
                borderPaint.setAntiAlias(true);
                borderPaint.setStyle(SkPaint::kStroke_Style);
                borderPaint.setStrokeWidth(1.0f);
                borderPaint.setColor(palette.menuSeparator);
                ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 4.0f, 4.0f, borderPaint);

                ctx.paint.setColor(palette.menuText);
                ctx.canvas->drawString(g_appState.hoveredUrl.c_str(), overlayX + padding, overlayY + overlayH - 7.0f, ctx.font, ctx.paint);
                }

                // Draw "Copied!" feedback overlay at the bottom right
                if (g_appState.copiedFeedbackTimeout > GetTickCount64()) {
                const char* msg = "Copied!";
                const float padding = 8.0f;
                ctx.font.setSize(mdviewer::GetCopiedOverlayFontSize(g_appState.baseFontSize));
                ctx.font.setTypeface(g_boldTypeface);

                SkRect textBounds;
                ctx.font.measureText(msg, strlen(msg), SkTextEncoding::kUTF8, &textBounds);

                const float overlayW = textBounds.width() + (padding * 2.0f);
                const float overlayH = 28.0f;
                const float overlayX = static_cast<float>(g_surface->width()) - overlayW - 10.0f;
                const float overlayY = static_cast<float>(g_surface->height()) - overlayH - 10.0f;

                SkPaint bPaint;
                bPaint.setAntiAlias(true);
                bPaint.setColor(palette.menuSelectedBackground);
                bPaint.setAlphaf(0.95f);
                ctx.canvas->drawRoundRect(SkRect::MakeXYWH(overlayX, overlayY, overlayW, overlayH), 6.0f, 6.0f, bPaint);

                ctx.paint.setColor(palette.menuSelectedText);
                ctx.canvas->drawString(msg, overlayX + padding, overlayY + overlayH - 8.0f, ctx.font, ctx.paint);
                }
                }

                mdviewer::win::PresentRasterSurface(hwnd, g_surface.get());
            }

            bool LoadFile(HWND hwnd, const std::filesystem::path& path, bool pushHistory) {
            if (!EnsureFontSystem()) {
            MessageBoxW(hwnd, L"Font initialization failed. The document cannot be rendered.", L"Error", MB_ICONERROR);
            return false;
            }

            RECT rect;
            GetClientRect(hwnd, &rect);
            const float width = static_cast<float>(rect.right - rect.left);

            auto imageSizeProvider = [](const std::string& url) -> std::pair<float, float> {
                auto it = g_imageCache.find(url);
                if (it != g_imageCache.end() && it->second) {
                    return {static_cast<float>(it->second->width()), static_cast<float>(it->second->height())};
                }
                return {0.0f, 0.0f};
            };

            const auto status = mdviewer::OpenDocument(
                g_appState,
                path,
                width,
                g_typeface.get(),
                g_appState.baseFontSize,
                [](const mdviewer::DocumentModel& docModel, const std::filesystem::path& baseDir) {
                    PreloadImages(docModel, baseDir);
                },
                imageSizeProvider,
                pushHistory);

            if (status == mdviewer::OpenDocumentStatus::BinaryFile) {
                MessageBoxW(hwnd, L"The file appears to be a binary file and cannot be opened internally.", L"Information", MB_ICONINFORMATION);
                return false;
            }
            if (status == mdviewer::OpenDocumentStatus::FileReadError) {
                MessageBoxW(hwnd, (L"Could not load file: " + path.wstring()).c_str(), L"Error", MB_ICONERROR);
                return false;
            }

            std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
            SetWindowTextW(hwnd, title.c_str());

            InvalidateRect(hwnd, NULL, FALSE);
            return true;
            }

            void OnGoBack(HWND hwnd) {
            if (const auto target = mdviewer::GetHistoryNavigationTarget(g_appState, mdviewer::HistoryDirection::Back)) {
            if (LoadFile(hwnd, target->path, false)) {
                mdviewer::CommitHistoryNavigation(g_appState, target->index);
            }
            }
            }

            void OnGoForward(HWND hwnd) {
            if (const auto target = mdviewer::GetHistoryNavigationTarget(g_appState, mdviewer::HistoryDirection::Forward)) {
            if (LoadFile(hwnd, target->path, false)) {
                mdviewer::CommitHistoryNavigation(g_appState, target->index);
            }
            }
            }

            void HandleLinkClick(HWND hwnd, const std::string& url, bool forceExternal) {
            if (url.empty()) return;

            const auto target = mdviewer::ResolveLinkTarget(g_appState.currentFilePath, url, forceExternal);
            switch (target.kind) {
                case mdviewer::LinkTargetKind::InternalDocument:
                    LoadFile(hwnd, target.path);
                    break;
                case mdviewer::LinkTargetKind::ExternalUrl:
                    ShellExecuteA(NULL, "open", target.externalUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                case mdviewer::LinkTargetKind::ExternalPath:
                    ShellExecuteW(NULL, L"open", target.path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                case mdviewer::LinkTargetKind::Invalid:
                default:
                    break;
            }
            }            void RelayoutCurrentDocument(HWND hwnd) {
            if (!EnsureFontSystem()) {
            return;
            }

            RECT rect;
            GetClientRect(hwnd, &rect);
            const float width = static_cast<float>(rect.right - rect.left);

            auto imageSizeProvider = [](const std::string& url) -> std::pair<float, float> {
            auto it = g_imageCache.find(url);
            if (it != g_imageCache.end() && it->second) {
                return {static_cast<float>(it->second->width()), static_cast<float>(it->second->height())};
            }
            return {0.0f, 0.0f};
            };

            if (!mdviewer::RelayoutDocument(
                    g_appState,
                    width,
                    g_typeface.get(),
                    g_appState.baseFontSize,
                    [](const mdviewer::DocumentModel& docModel, const std::filesystem::path& baseDir) {
                        PreloadImages(docModel, baseDir);
                    },
                    imageSizeProvider)) {
            return;
            }

            ClampScrollOffset(hwnd);
            }
            }

            void SetBaseFontSize(HWND hwnd, float baseFontSize) {
            const float clampedFontSize = mdviewer::ClampBaseFontSize(baseFontSize);
            if (std::abs(clampedFontSize - g_appState.baseFontSize) < 0.01f) {
            return;
            }

            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.baseFontSize = clampedFontSize;
            }

            RelayoutCurrentDocument(hwnd);
            SaveCurrentConfig();
            SyncMenuState(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            }

            void AdjustBaseFontSize(HWND hwnd, float delta) {
            SetBaseFontSize(hwnd, g_appState.baseFontSize + delta);
            }

            void ApplySelectedFont(HWND hwnd, const std::wstring& familyName) {    const std::wstring previousFamily = g_selectedFontFamily;
    g_selectedFontFamily = familyName;
    ResetResolvedTypefaces();

    if (!EnsureFontSystem()) {
        g_selectedFontFamily = previousFamily;
        ResetResolvedTypefaces();
        EnsureFontSystem();
        MessageBoxW(hwnd, L"The selected font could not be loaded.", L"Error", MB_ICONERROR);
        return;
    }

    RelayoutCurrentDocument(hwnd);
    SaveCurrentConfig();
    SyncMenuState(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

void ApplyTheme(HWND hwnd, mdviewer::ThemeMode theme) {
    {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        g_appState.theme = theme;
        g_appState.needsRepaint = true;
    }
    SaveCurrentConfig();
    SyncMenuState(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
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

    const float contentY = static_cast<float>(y) - GetContentTopInset();
    const float viewportHeight = GetViewportHeight(hwnd);
    if (contentY < 0) {
        g_appState.scrollOffset = std::max(g_appState.scrollOffset + contentY, 0.0f);
    } else if (contentY > viewportHeight) {
        g_appState.scrollOffset = std::min(g_appState.scrollOffset + (contentY - viewportHeight), GetMaxScroll(hwnd));
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
            SyncMenuState(hwnd);
            return 0;
        case WM_DESTROY:
            StopAutoScroll(hwnd);
            mdviewer::win::CleanupMenus();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            Render(hwnd);
            return 0;
        case WM_MEASUREITEM:
            if (mdviewer::win::HandleMeasureMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (mdviewer::win::HandleDrawMenuItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), GetCurrentThemePalette())) {
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES:
            OnDropFiles(hwnd, (HDROP)wParam);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case mdviewer::win::kCommandOpenFile:
                    if (const auto path = ShowOpenFileDialog(hwnd)) {
                        LoadFile(hwnd, *path);
                    }
                    return 0;
                case mdviewer::win::kCommandExit:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case mdviewer::win::kCommandSelectFont:
                    if (const auto familyName = ShowFontDialog(hwnd)) {
                        ApplySelectedFont(hwnd, *familyName);
                    }
                    return 0;
                case mdviewer::win::kCommandUseDefaultFont:
                    if (!g_selectedFontFamily.empty()) {
                        ApplySelectedFont(hwnd, L"");
                    }
                    return 0;
                case mdviewer::win::kCommandThemeLight:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Light);
                    return 0;
                case mdviewer::win::kCommandThemeSepia:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Sepia);
                    return 0;
                case mdviewer::win::kCommandThemeDark:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Dark);
                    return 0;
                case mdviewer::win::kCommandGoBack:
                    OnGoBack(hwnd);
                    return 0;
                case mdviewer::win::kCommandGoForward:
                    OnGoForward(hwnd);
                    return 0;
                case mdviewer::win::kCommandZoomOut:
                    AdjustBaseFontSize(hwnd, -1.0f);
                    return 0;
                case mdviewer::win::kCommandZoomIn:
                    AdjustBaseFontSize(hwnd, 1.0f);
                    return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int topMenuIndex = mdviewer::win::HitTestTopMenu(hwnd, x, y, g_surface->width());
            if (topMenuIndex != -1) {
                StopAutoScroll(hwnd);
                SetFocus(hwnd);
                if (const UINT command = mdviewer::win::OpenTopMenu(hwnd, topMenuIndex); command != 0) {
                    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
                }
                return 0;
            }

            auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));

            // Check code block copy buttons
            {
                std::lock_guard<std::mutex> buttonLock(g_appState.mtx);
                const float docX = static_cast<float>(x);
                const float docY = static_cast<float>(y) - GetContentTopInset() + g_appState.scrollOffset;
                for (const auto& btn : g_appState.codeBlockButtons) {
                    if (btn.first.contains(docX, docY)) {
                        const size_t start = btn.second.first;
                        const size_t end = btn.second.second;
                        if (end > start && end <= g_appState.docLayout.plainText.size()) {
                            CopyTextToClipboard(hwnd, g_appState.docLayout.plainText.substr(start, end - start));
                            
                            // Show "Copied!" feedback
                            g_appState.copiedFeedbackTimeout = GetTickCount64() + 2000;
                            SetTimer(hwnd, kCopiedFeedbackTimerId, 2000, nullptr);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                        return 0;
                    }
                }
            }

            StopAutoScroll(hwnd);
            SetFocus(hwnd);
            SetCapture(hwnd);
            if (const auto thumb = GetScrollbarThumbRect(hwnd); thumb && thumb->contains(static_cast<float>(x), static_cast<float>(y))) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = static_cast<float>(y) - thumb->top();
                g_appState.pendingLinkClick = false;
                g_appState.pendingLinkForceExternal = false;
                g_appState.pendingLinkPressX = 0;
                g_appState.pendingLinkPressY = 0;
                g_appState.pendingLinkUrl.clear();
            } else if (x >= g_surface->width() - static_cast<int>(kScrollbarWidth + (kScrollbarMargin * 2.0f)) && GetScrollbarThumbRect(hwnd)) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = GetScrollbarThumbRect(hwnd)->height() * 0.5f;
                g_appState.pendingLinkClick = false;
                g_appState.pendingLinkForceExternal = false;
                g_appState.pendingLinkPressX = 0;
                g_appState.pendingLinkPressY = 0;
                g_appState.pendingLinkUrl.clear();
                UpdateScrollOffsetFromThumb(hwnd, y);
            } else {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isSelecting = true;
                g_appState.isDraggingScrollbar = false;
                if (hit.valid) {
                    g_appState.selectionAnchor = hit.position;
                    g_appState.selectionFocus = hit.position;
                } else {
                    g_appState.selectionAnchor = 0;
                    g_appState.selectionFocus = 0;
                }
                g_appState.pendingLinkClick = hit.valid && !hit.url.empty();
                g_appState.pendingLinkForceExternal = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                g_appState.pendingLinkPressX = x;
                g_appState.pendingLinkPressY = y;
                g_appState.pendingLinkUrl = g_appState.pendingLinkClick ? hit.url : "";
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_XBUTTONDOWN:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
                OnGoBack(hwnd);
            } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
                OnGoForward(hwnd);
            }
            return TRUE;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (mdviewer::win::UpdateTopMenuHover(hwnd, x, y, g_surface->width())) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            // Update hovered URL
            {
                auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                if (hit.valid && !hit.url.empty()) {
                    if (g_appState.hoveredUrl != hit.url) {
                        g_appState.hoveredUrl = hit.url;
                        g_appState.needsRepaint = true;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (!g_appState.hoveredUrl.empty()) {
                    g_appState.hoveredUrl = "";
                    g_appState.needsRepaint = true;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }

            bool isAutoScrolling = false;
            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                isAutoScrolling = g_appState.isAutoScrolling;
                if (isAutoScrolling) {
                    g_appState.autoScrollCursorX = static_cast<float>(GET_X_LPARAM(lParam));
                    g_appState.autoScrollCursorY = static_cast<float>(GET_Y_LPARAM(lParam));
                }
            }
            if (isAutoScrolling) {
                TickAutoScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if ((wParam & MK_LBUTTON) == 0) {
                return 0;
            }

            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.isDraggingScrollbar) {
                UpdateScrollOffsetFromThumb(hwnd, GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_appState.isSelecting) {
                if (g_appState.pendingLinkClick) {
                    const int dx = GET_X_LPARAM(lParam) - g_appState.pendingLinkPressX;
                    const int dy = GET_Y_LPARAM(lParam) - g_appState.pendingLinkPressY;
                    if ((dx * dx) + (dy * dy) <= (kLinkClickSlop * kLinkClickSlop)) {
                        return 0;
                    }
                    g_appState.pendingLinkClick = false;
                    g_appState.pendingLinkForceExternal = false;
                    g_appState.pendingLinkUrl.clear();
                }
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const auto releaseHit = HitTestText(static_cast<float>(x), static_cast<float>(y));

            bool shouldUpdateSelection = false;
            bool activateLink = false;
            bool forceExternal = false;
            std::string linkUrl;

            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = false;
                if (g_appState.isSelecting) {
                    if (g_appState.pendingLinkClick) {
                        activateLink = releaseHit.valid &&
                                       !releaseHit.url.empty() &&
                                       releaseHit.url == g_appState.pendingLinkUrl;
                        forceExternal = g_appState.pendingLinkForceExternal;
                        linkUrl = g_appState.pendingLinkUrl;
                        if (activateLink) {
                            g_appState.selectionAnchor = 0;
                            g_appState.selectionFocus = 0;
                        } else {
                            shouldUpdateSelection = true;
                        }
                    } else {
                        shouldUpdateSelection = true;
                    }
                }

                g_appState.pendingLinkClick = false;
                g_appState.pendingLinkForceExternal = false;
                g_appState.pendingLinkUrl.clear();
                g_appState.pendingLinkPressX = 0;
                g_appState.pendingLinkPressY = 0;
            }

            ReleaseCapture();

            if (shouldUpdateSelection) {
                UpdateSelectionFromPoint(hwnd, x, y);
            }

            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isSelecting = false;
                if (!g_appState.HasSelection()) {
                    g_appState.selectionAnchor = 0;
                    g_appState.selectionFocus = 0;
                }
            }

            if (activateLink) {
                HandleLinkClick(hwnd, linkUrl, forceExternal);
            }

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MBUTTONDOWN:
            if (GET_Y_LPARAM(lParam) < mdviewer::win::kMenuBarHeight) {
                return 0;
            }
            SetFocus(hwnd);
            if (g_appState.isAutoScrolling) {
                StopAutoScroll(hwnd);
            } else {
                StartAutoScroll(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_MBUTTONUP:
            return 0;
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
            if (wParam == VK_ESCAPE) {
                StopAutoScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (wParam == VK_OEM_PLUS || wParam == VK_ADD) {
                    AdjustBaseFontSize(hwnd, 1.0f);
                    return 0;
                }
                if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) {
                    AdjustBaseFontSize(hwnd, -1.0f);
                    return 0;
                }
            }
            if (wParam == VK_BACK) {
                OnGoBack(hwnd);
                return 0;
            }
            if (GetKeyState(VK_MENU) & 0x8000) {
                if (wParam == VK_LEFT) {
                    OnGoBack(hwnd);
                    return 0;
                }
                if (wParam == VK_RIGHT) {
                    OnGoForward(hwnd);
                    return 0;
                }
            } else if (!g_appState.HasSelection()) {
                // Standalone arrow keys for navigation if no selection is active
                if (wParam == VK_LEFT) {
                    OnGoBack(hwnd);
                    return 0;
                }
                if (wParam == VK_RIGHT) {
                    OnGoForward(hwnd);
                    return 0;
                }
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                CopySelection(hwnd);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kAutoScrollTimerId) {
                if (TickAutoScroll(hwnd)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            if (wParam == kCopiedFeedbackTimerId) {
                {
                    std::lock_guard<std::mutex> timerLock(g_appState.mtx);
                    g_appState.copiedFeedbackTimeout = 0;
                }
                KillTimer(hwnd, kCopiedFeedbackTimerId);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd) {
                StopAutoScroll(hwnd);
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.pendingLinkClick = false;
                g_appState.pendingLinkForceExternal = false;
                g_appState.pendingLinkPressX = 0;
                g_appState.pendingLinkPressY = 0;
                g_appState.pendingLinkUrl.clear();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_SIZE: {
            UpdateSurface(hwnd);
            RelayoutCurrentDocument(hwnd);
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

    LoadInitialConfig();

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";

    auto* largeIcon = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    auto* smallIcon = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = largeIcon;
    wc.hIconSm = smallIcon ? smallIcon : largeIcon;

    RegisterClassExW(&wc);

    if (!mdviewer::win::CreateMenus(GetCurrentThemePalette())) {
        MessageBoxW(nullptr, L"Menu initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Markdown Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kInitialWindowWidth, kInitialWindowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        mdviewer::win::CleanupMenus();
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
