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
#include "platform/win/win_window.h"
#include "render/document_renderer.h"
#include "render/theme.h"
#include "render/typography.h"
#include "util/file_io.h"
#include "util/skia_font_utils.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"

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

    constexpr float kCodeBlockMarginY = 16.0f;
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

    struct CachedImageEntry {
        sk_sp<SkImage> baseImage;
        std::map<uint64_t, sk_sp<SkImage>> scaledImages;
    };

    std::map<std::string, CachedImageEntry> g_imageCache;

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

    void ConfigureFont(RenderContext& ctx, mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        mdviewer::ConfigureDocumentFont(
            ctx.font,
            mdviewer::DocumentTypefaceSet{
                .regular = g_typeface.get(),
                .bold = g_boldTypeface.get(),
                .heading = g_headingTypeface.get(),
                .code = g_codeTypeface.get(),
            },
            blockType,
            inlineStyle,
            g_appState.baseFontSize);
    }

    float GetContentX(const mdviewer::BlockLayout& block) {
        return mdviewer::GetDocumentContentX(block);
    }

    size_t GetSelectionStart() {
        return mdviewer::GetSelectionStart(g_appState);
    }

    size_t GetSelectionEnd() {
        return mdviewer::GetSelectionEnd(g_appState);
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

    mdviewer::InteractionTextHit ToInteractionHit(const TextHit& hit) {
        return mdviewer::InteractionTextHit{
            .position = hit.position,
            .valid = hit.valid,
            .url = hit.url,
        };
    }

    void StopAutoScroll(HWND hwnd) {
        const bool shouldReleaseCapture = GetCapture() == hwnd;
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            mdviewer::StopAutoScrollState(g_appState);
        }
        KillTimer(hwnd, kAutoScrollTimerId);
        if (shouldReleaseCapture) {
            ReleaseCapture();
        }
    }

    void StartAutoScroll(HWND hwnd, int x, int y) {
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            mdviewer::StartAutoScrollState(g_appState, static_cast<float>(x), static_cast<float>(y));
        }
        SetCapture(hwnd);
        SetTimer(hwnd, kAutoScrollTimerId, kAutoScrollTimerMs, nullptr);
    }

    bool TickAutoScroll(HWND hwnd) {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        return mdviewer::TickAutoScroll(g_appState, GetMaxScroll(hwnd), kAutoScrollDeadZone);
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

    TextHit HitTestText(float x, float viewportY) {
        TextHit hit;
        if (!EnsureFontSystem()) {
            return hit;
        }

        RenderContext ctx;
        ctx.canvas = nullptr;
        ctx.paint.setAntiAlias(true);
        ctx.font.setTypeface(g_typeface);

        const auto sharedHit = mdviewer::HitTestDocument(
            g_appState.docLayout,
            g_appState.scrollOffset,
            GetContentTopInset(),
            x,
            viewportY,
            mdviewer::HitTestCallbacks{
                .get_content_x = [&](const mdviewer::BlockLayout& block) {
                    return GetContentX(block);
                },
                .get_run_visual_width = [&](const mdviewer::BlockLayout& block, const mdviewer::RunLayout& run) {
                    return GetRunVisualWidth(ctx, block.type, run);
                },
                .find_text_position_in_run = [&](const mdviewer::BlockLayout& block, const mdviewer::RunLayout& run, float xInRun) {
                    return FindTextPositionInRun(ctx, block.type, run, xInRun);
                },
            });
        hit.position = sharedHit.position;
        hit.valid = sharedHit.valid;
        hit.url = sharedHit.url;
        hit.style = sharedHit.style;
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

    uint64_t MakeScaledImageKey(float width, float height) {
        const uint32_t roundedWidth = static_cast<uint32_t>(std::max(1.0f, std::round(width)));
        const uint32_t roundedHeight = static_cast<uint32_t>(std::max(1.0f, std::round(height)));
        return (static_cast<uint64_t>(roundedWidth) << 32) | roundedHeight;
    }

    sk_sp<SkImage> CreateRasterImageFromFile(const std::filesystem::path& imagePath) {
        if (!std::filesystem::exists(imagePath)) {
            return nullptr;
        }

        auto data = SkData::MakeFromFileName(imagePath.string().c_str());
        if (!data) {
            return nullptr;
        }

        auto image = SkImages::DeferredFromEncodedData(data);
        if (!image) {
            return nullptr;
        }

        return image->makeRasterImage();
    }

    sk_sp<SkImage> GetOrLoadBaseImage(const std::string& url, const std::filesystem::path& baseDir) {
        auto& entry = g_imageCache[url];
        if (entry.baseImage) {
            return entry.baseImage;
        }

        std::filesystem::path imagePath = url;
        if (imagePath.is_relative()) {
            imagePath = baseDir / url;
        }

        entry.baseImage = CreateRasterImageFromFile(imagePath);
        return entry.baseImage;
    }

    sk_sp<SkImage> GetCachedScaledImage(const std::string& url, const std::filesystem::path& baseDir, float displayWidth, float displayHeight) {
        sk_sp<SkImage> baseImage = GetOrLoadBaseImage(url, baseDir);
        if (!baseImage) {
            return nullptr;
        }

        const int targetWidth = std::max(1, static_cast<int>(std::round(displayWidth)));
        const int targetHeight = std::max(1, static_cast<int>(std::round(displayHeight)));
        if (baseImage->width() == targetWidth && baseImage->height() == targetHeight) {
            return baseImage;
        }

        auto& entry = g_imageCache[url];
        const uint64_t scaledKey = MakeScaledImageKey(static_cast<float>(targetWidth), static_cast<float>(targetHeight));
        auto it = entry.scaledImages.find(scaledKey);
        if (it != entry.scaledImages.end()) {
            return it->second;
        }

        const auto info = SkImageInfo::MakeN32Premul(targetWidth, targetHeight);
        auto surface = SkSurfaces::Raster(info);
        if (!surface) {
            return baseImage;
        }

        SkCanvas* scaleCanvas = surface->getCanvas();
        scaleCanvas->clear(SK_ColorTRANSPARENT);
        scaleCanvas->drawImageRect(
            baseImage,
            SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight)),
            SkSamplingOptions(SkFilterMode::kLinear));

        auto scaledImage = surface->makeImageSnapshot();
        if (scaledImage) {
            scaledImage = scaledImage->makeRasterImage();
        }
        if (scaledImage) {
            entry.scaledImages[scaledKey] = scaledImage;
            return scaledImage;
        }

        return baseImage;
    }

    void PreloadImage(const std::string& url, const std::filesystem::path& baseDir) {
        GetOrLoadBaseImage(url, baseDir);
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
                mdviewer::RenderDocumentScene(
                    mdviewer::DocumentSceneParams{
                        .canvas = canvas,
                        .appState = &g_appState,
                        .palette = palette,
                        .typefaces = mdviewer::DocumentTypefaceSet{
                            .regular = g_typeface.get(),
                            .bold = g_boldTypeface.get(),
                            .heading = g_headingTypeface.get(),
                            .code = g_codeTypeface.get(),
                        },
                        .baseFontSize = g_appState.baseFontSize,
                        .contentTopInset = GetContentTopInset(),
                        .viewportHeight = GetViewportHeight(hwnd),
                        .surfaceWidth = static_cast<float>(g_surface->width()),
                        .surfaceHeight = static_cast<float>(g_surface->height()),
                        .scrollbarWidth = kScrollbarWidth,
                        .scrollbarMargin = kScrollbarMargin,
                        .currentTickCount = GetTickCount64(),
                        .visibleDocumentTop = g_appState.scrollOffset,
                        .visibleDocumentBottom = g_appState.scrollOffset + GetViewportHeight(hwnd),
                        .scrollbarThumbRect = GetScrollbarThumbRect(hwnd),
                        .resolveImage = [&](const std::string& url, float displayWidth, float displayHeight) -> sk_sp<SkImage> {
                            return GetCachedScaledImage(
                                url,
                                g_appState.currentFilePath.parent_path(),
                                displayWidth,
                                displayHeight);
                        },
                        .addCodeBlockButton = [&](const SkRect& rect, size_t start, size_t end) {
                            g_appState.codeBlockButtons.push_back({rect, {start, end}});
                        },
                    });
            }

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
                if (it != g_imageCache.end() && it->second.baseImage) {
                    return {static_cast<float>(it->second.baseImage->width()), static_cast<float>(it->second.baseImage->height())};
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
            if (it != g_imageCache.end() && it->second.baseImage) {
                return {static_cast<float>(it->second.baseImage->width()), static_cast<float>(it->second.baseImage->height())};
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
    mdviewer::UpdateSelectionFromHit(
        g_appState,
        ToInteractionHit(HitTestText(static_cast<float>(x), static_cast<float>(y))),
        static_cast<float>(y) - GetContentTopInset(),
        GetViewportHeight(hwnd),
        GetMaxScroll(hwnd));
}

void UpdateScrollOffsetFromThumb(HWND hwnd, int mouseY) {
    const auto thumb = GetScrollbarThumbRect(hwnd);
    if (!thumb) {
        return;
    }

    mdviewer::UpdateScrollOffsetFromThumb(
        g_appState,
        mouseY,
        *thumb,
        GetViewportHeight(hwnd),
        GetMaxScroll(hwnd),
        kScrollbarMargin);
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

mdviewer::InteractionKey TranslateInteractionKey(WPARAM wParam) {
    switch (wParam) {
        case VK_ESCAPE:
            return mdviewer::InteractionKey::Escape;
        case VK_OEM_PLUS:
        case VK_ADD:
            return mdviewer::InteractionKey::ZoomIn;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            return mdviewer::InteractionKey::ZoomOut;
        case VK_LEFT:
            return mdviewer::InteractionKey::Left;
        case VK_RIGHT:
            return mdviewer::InteractionKey::Right;
        case VK_BACK:
            return mdviewer::InteractionKey::Back;
        case 'C':
        case 'c':
            return mdviewer::InteractionKey::Copy;
        default:
            return mdviewer::InteractionKey::Unknown;
    }
}

bool ExecuteKeyCommand(HWND hwnd, const mdviewer::KeyCommandResult& command) {
    if (command.stopAutoScroll) {
        StopAutoScroll(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
    if (command.zoomIn) {
        AdjustBaseFontSize(hwnd, 1.0f);
    }
    if (command.zoomOut) {
        AdjustBaseFontSize(hwnd, -1.0f);
    }
    if (command.goBack) {
        OnGoBack(hwnd);
    }
    if (command.goForward) {
        OnGoForward(hwnd);
    }
    if (command.copySelection) {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        CopySelection(hwnd);
    }
    return command.handled;
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
            if (mdviewer::win::DispatchWindowCommand(
                    LOWORD(wParam),
                    mdviewer::win::WindowCommandHandlers{
                        .openFile = [&]() {
                    if (const auto path = ShowOpenFileDialog(hwnd)) {
                        LoadFile(hwnd, *path);
                    }
                        },
                        .exitApp = [&]() {
                            PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        },
                        .selectFont = [&]() {
                    if (const auto familyName = ShowFontDialog(hwnd)) {
                        ApplySelectedFont(hwnd, *familyName);
                    }
                        },
                        .useDefaultFont = [&]() {
                    if (!g_selectedFontFamily.empty()) {
                        ApplySelectedFont(hwnd, L"");
                    }
                        },
                        .applyLightTheme = [&]() {
                            ApplyTheme(hwnd, mdviewer::ThemeMode::Light);
                        },
                        .applySepiaTheme = [&]() {
                            ApplyTheme(hwnd, mdviewer::ThemeMode::Sepia);
                        },
                        .applyDarkTheme = [&]() {
                            ApplyTheme(hwnd, mdviewer::ThemeMode::Dark);
                        },
                        .goBack = [&]() {
                            OnGoBack(hwnd);
                        },
                        .goForward = [&]() {
                            OnGoForward(hwnd);
                        },
                        .zoomOut = [&]() {
                            AdjustBaseFontSize(hwnd, -1.0f);
                        },
                        .zoomIn = [&]() {
                            AdjustBaseFontSize(hwnd, 1.0f);
                        },
                    })) {
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
                mdviewer::BeginScrollbarDrag(g_appState, static_cast<float>(y) - thumb->top());
            } else if (x >= g_surface->width() - static_cast<int>(kScrollbarWidth + (kScrollbarMargin * 2.0f)) && GetScrollbarThumbRect(hwnd)) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                mdviewer::BeginScrollbarDrag(g_appState, GetScrollbarThumbRect(hwnd)->height() * 0.5f);
                UpdateScrollOffsetFromThumb(hwnd, y);
            } else {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                mdviewer::BeginSelection(
                    g_appState,
                    ToInteractionHit(hit),
                    (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                    x,
                    y);
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
                if (mdviewer::UpdateHoveredUrl(g_appState, ToInteractionHit(hit))) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }

            bool isAutoScrolling = false;
            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                isAutoScrolling = g_appState.isAutoScrolling;
                if (isAutoScrolling) {
                    mdviewer::UpdateAutoScrollCursor(
                        g_appState,
                        static_cast<float>(GET_X_LPARAM(lParam)),
                        static_cast<float>(GET_Y_LPARAM(lParam)));
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
                if (!mdviewer::CancelPendingLinkIfDragged(g_appState, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kLinkClickSlop) &&
                    g_appState.pendingLinkClick) {
                    return 0;
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
                const auto result = mdviewer::FinishPrimaryPointerInteraction(g_appState, ToInteractionHit(releaseHit));
                shouldUpdateSelection = result.shouldUpdateSelection;
                activateLink = result.activateLink;
                forceExternal = result.forceExternal;
                linkUrl = result.linkUrl;
            }

            ReleaseCapture();

            if (shouldUpdateSelection) {
                UpdateSelectionFromPoint(hwnd, x, y);
            }

            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                mdviewer::FinalizeSelectionInteraction(g_appState);
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
                mdviewer::ApplyWheelScroll(g_appState, static_cast<float>(delta), GetMaxScroll(hwnd));
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (ExecuteKeyCommand(
                    hwnd,
                    mdviewer::HandleKeyDown(
                        g_appState,
                        mdviewer::KeyEvent{
                            .key = TranslateInteractionKey(wParam),
                            .ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                            .alt = (GetKeyState(VK_MENU) & 0x8000) != 0,
                        }))) {
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
                mdviewer::ClearPendingLinkState(g_appState);
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
    if (!mdviewer::win::RegisterMainWindowClass(hInstance, WindowProc, kAppIconResourceId, CLASS_NAME)) {
        MessageBoxW(nullptr, L"Window class registration failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    if (!mdviewer::win::CreateMenus(GetCurrentThemePalette())) {
        MessageBoxW(nullptr, L"Menu initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    HWND hwnd = mdviewer::win::CreateMainWindow(
        hInstance,
        CLASS_NAME,
        L"Markdown Viewer",
        kInitialWindowWidth,
        kInitialWindowHeight);

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

    const int exitCode = mdviewer::win::RunMessageLoop();

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return exitCode;
}
