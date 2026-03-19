#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
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

#include "app/app_state.h"
#include "app/viewer_controller.h"
#include "platform/win/win_clipboard.h"
#include "platform/win/win_file_dialog.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_shell.h"
#include "platform/win/win_surface.h"
#include "platform/win/win_viewer_host.h"
#include "platform/win/win_window.h"
#include "render/document_typefaces.h"
#include "render/document_renderer.h"
#include "render/image_cache.h"
#include "render/theme.h"
#include "render/typography.h"
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
#include "include/core/SkImage.h"
#pragma warning(pop)

namespace {
    mdviewer::ViewerController g_viewerController;
    mdviewer::AppState& g_appState = g_viewerController.GetMutableAppState();
    sk_sp<SkSurface> g_surface;
    mdviewer::DocumentTypefaceCache g_typefaces;
    mdviewer::DocumentImageCache g_imageCache;

    std::wstring Utf8ToWide(const std::string& text);
    mdviewer::win::ViewerHostContext& GetHostContext();

    constexpr float kCodeBlockMarginY = 16.0f;
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

    std::wstring GetSelectedFontFamily() {
        const std::string& fontFamilyUtf8 = g_viewerController.GetFontFamilyUtf8();
        if (fontFamilyUtf8.empty()) {
            return {};
        }
        return Utf8ToWide(fontFamilyUtf8);
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

    mdviewer::win::ViewerHostContext& GetHostContext() {
        static mdviewer::win::ViewerHostContext context{
            .controller = g_viewerController,
            .surface = g_surface,
            .typefaces = g_typefaces,
            .imageCache = g_imageCache,
        };
        return context;
    }

    void ConfigureFont(RenderContext& ctx, mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        mdviewer::ConfigureDocumentFont(
            ctx.font,
            mdviewer::win::GetDocumentTypefaces(GetHostContext()),
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
        return mdviewer::TickAutoScroll(g_appState, mdviewer::win::GetMaxScroll(hwnd, GetHostContext()), kAutoScrollDeadZone);
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
        if (!mdviewer::win::EnsureFontSystem(GetHostContext())) {
            return hit;
        }

        RenderContext ctx;
        ctx.canvas = nullptr;
        ctx.paint.setAntiAlias(true);
        ctx.font.setTypeface(sk_ref_sp(mdviewer::win::GetRegularTypeface(GetHostContext())));

        const auto sharedHit = mdviewer::HitTestDocument(
            g_appState.docLayout,
            g_appState.scrollOffset,
            mdviewer::win::GetContentTopInset(),
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

void OnDropFiles(HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (count > 0) {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            mdviewer::win::LoadFile(hwnd, GetHostContext(), path);
        }
    }
    DragFinish(hDrop);
}

void UpdateSelectionFromPoint(HWND hwnd, int x, int y) {
    mdviewer::UpdateSelectionFromHit(
        g_appState,
        ToInteractionHit(HitTestText(static_cast<float>(x), static_cast<float>(y))),
        static_cast<float>(y) - mdviewer::win::GetContentTopInset(),
        mdviewer::win::GetViewportHeight(hwnd, GetHostContext()),
        mdviewer::win::GetMaxScroll(hwnd, GetHostContext()));
}

void UpdateScrollOffsetFromThumb(HWND hwnd, int mouseY) {
    const auto thumb = mdviewer::win::GetScrollbarThumbRect(hwnd, GetHostContext());
    if (!thumb) {
        return;
    }

    mdviewer::UpdateScrollOffsetFromThumb(
        g_appState,
        mouseY,
        *thumb,
        mdviewer::win::GetViewportHeight(hwnd, GetHostContext()),
        mdviewer::win::GetMaxScroll(hwnd, GetHostContext()),
        mdviewer::win::kScrollbarMargin);
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

    return mdviewer::win::CopyUtf8TextToClipboard(
        hwnd,
        g_appState.docLayout.plainText.substr(selectionStart, selectionEnd - selectionStart));
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
        mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), 1.0f);
    }
    if (command.zoomOut) {
        mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), -1.0f);
    }
    if (command.goBack) {
        mdviewer::win::GoBack(hwnd, GetHostContext());
    }
    if (command.goForward) {
        mdviewer::win::GoForward(hwnd, GetHostContext());
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
            mdviewer::win::UpdateSurface(hwnd, GetHostContext());
            mdviewer::win::SyncMenuState(hwnd, GetHostContext());
            return 0;
        case WM_DESTROY:
            StopAutoScroll(hwnd);
            mdviewer::win::CleanupMenus();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            mdviewer::win::Render(hwnd, GetHostContext());
            return 0;
        case WM_MEASUREITEM:
            if (mdviewer::win::HandleMeasureMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (mdviewer::win::HandleDrawMenuItem(
                    reinterpret_cast<DRAWITEMSTRUCT*>(lParam),
                    mdviewer::win::GetCurrentThemePalette(GetHostContext()))) {
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
                    if (const auto path = mdviewer::win::ShowOpenFileDialog(hwnd)) {
                        mdviewer::win::LoadFile(hwnd, GetHostContext(), *path);
                    }
                        },
                        .openRecentFile = [&](const std::filesystem::path& path) {
                            mdviewer::win::LoadFile(hwnd, GetHostContext(), path);
                        },
                        .exitApp = [&]() {
                            PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        },
                        .selectFont = [&]() {
                    if (const auto familyName = mdviewer::win::ShowFontDialog(hwnd, GetSelectedFontFamily())) {
                        mdviewer::win::ApplySelectedFont(hwnd, GetHostContext(), WideToUtf8(*familyName));
                    }
                        },
                        .useDefaultFont = [&]() {
                    if (g_viewerController.HasCustomFontFamily()) {
                        mdviewer::win::ApplySelectedFont(hwnd, GetHostContext(), "");
                    }
                        },
                        .applyLightTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Light);
                        },
                        .applySepiaTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Sepia);
                        },
                        .applyDarkTheme = [&]() {
                            mdviewer::win::ApplyTheme(hwnd, GetHostContext(), mdviewer::ThemeMode::Dark);
                        },
                        .goBack = [&]() {
                            mdviewer::win::GoBack(hwnd, GetHostContext());
                        },
                        .goForward = [&]() {
                            mdviewer::win::GoForward(hwnd, GetHostContext());
                        },
                        .zoomOut = [&]() {
                            mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), -1.0f);
                        },
                        .zoomIn = [&]() {
                            mdviewer::win::AdjustBaseFontSize(hwnd, GetHostContext(), 1.0f);
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
                const float docY = static_cast<float>(y) - mdviewer::win::GetContentTopInset() + g_appState.scrollOffset;
                for (const auto& btn : g_appState.codeBlockButtons) {
                    if (btn.first.contains(docX, docY)) {
                        const size_t start = btn.second.first;
                        const size_t end = btn.second.second;
                        if (end > start && end <= g_appState.docLayout.plainText.size()) {
                            mdviewer::win::CopyUtf8TextToClipboard(
                                hwnd,
                                g_appState.docLayout.plainText.substr(start, end - start));
                            
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
            if (const auto thumb = mdviewer::win::GetScrollbarThumbRect(hwnd, GetHostContext());
                thumb && thumb->contains(static_cast<float>(x), static_cast<float>(y))) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                mdviewer::BeginScrollbarDrag(g_appState, static_cast<float>(y) - thumb->top());
            } else if (
                x >= g_surface->width() - static_cast<int>(mdviewer::win::kScrollbarWidth + (mdviewer::win::kScrollbarMargin * 2.0f)) &&
                mdviewer::win::GetScrollbarThumbRect(hwnd, GetHostContext())) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                mdviewer::BeginScrollbarDrag(
                    g_appState,
                    mdviewer::win::GetScrollbarThumbRect(hwnd, GetHostContext())->height() * 0.5f);
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
                mdviewer::win::GoBack(hwnd, GetHostContext());
            } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
                mdviewer::win::GoForward(hwnd, GetHostContext());
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
                mdviewer::win::ClampScrollOffset(hwnd, GetHostContext());
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_appState.isSelecting) {
                if (!mdviewer::CancelPendingLinkIfDragged(g_appState, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), kLinkClickSlop) &&
                    g_appState.pendingLinkClick) {
                    return 0;
                }
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                mdviewer::win::ClampScrollOffset(hwnd, GetHostContext());
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
                mdviewer::win::HandleLinkClick(hwnd, GetHostContext(), linkUrl, forceExternal);
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
                mdviewer::ApplyWheelScroll(
                    g_appState,
                    static_cast<float>(delta),
                    mdviewer::win::GetMaxScroll(hwnd, GetHostContext()));
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
            mdviewer::win::UpdateSurface(hwnd, GetHostContext());
            mdviewer::win::RelayoutCurrentDocument(hwnd, GetHostContext());
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

} // namespace

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

    g_viewerController.SetConfigPath(GetExecutableConfigPath());
    g_viewerController.LoadConfig();

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";
    if (!mdviewer::win::RegisterMainWindowClass(hInstance, WindowProc, kAppIconResourceId, CLASS_NAME)) {
        MessageBoxW(nullptr, L"Window class registration failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    if (!mdviewer::win::CreateMenus(mdviewer::win::GetCurrentThemePalette(GetHostContext()))) {
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

    if (!mdviewer::win::EnsureFontSystem(GetHostContext())) {
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
            mdviewer::win::LoadFile(hwnd, GetHostContext(), argv[1]);
        }
        LocalFree(argv);
    }

    const int exitCode = mdviewer::win::RunMessageLoop();

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return exitCode;
}
