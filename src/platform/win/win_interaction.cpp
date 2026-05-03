#include "platform/win/win_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <string>

#include "platform/win/win_clipboard.h"
#include "platform/win/win_context_menu.h"
#include "platform/win/win_menu.h"
#include "render/document_renderer.h"
#include "view/document_context_menu.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#pragma warning(pop)

namespace mdviewer::win {

namespace {

struct RenderContext {
    SkPaint paint;
    SkFont font;
};

DocumentTextHit HitTestText(ViewerInteractionContext& context, float x, float viewportY) {
    DocumentTextHit hit;
    if (!EnsureFontSystem(context.host)) {
        return hit;
    }

    AppState& appState = GetAppState(context.host);

    RenderContext renderContext;
    renderContext.paint.setAntiAlias(true);
    renderContext.font.setTypeface(sk_ref_sp(GetRegularTypeface(context.host)));

    const auto typefaces = GetDocumentTypefaces(context.host);
    const auto hitTest = HitTestDocument(
        appState.docLayout,
        appState.scrollOffset,
        GetContentTopInset(),
        x,
        viewportY,
        HitTestCallbacks{
            .get_run_visual_width = [&](const BlockLayout& block, const LineLayout& line, const RunLayout& run) {
                (void)line;
                if (run.style == InlineStyle::Image) {
                    return run.imageWidth;
                }

                ConfigureDocumentFont(
                    renderContext.font,
                    typefaces,
                    block.type,
                    run.style,
                    appState.baseFontSize);
                return renderContext.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
            },
            .find_text_position_in_run = [&](const BlockLayout& block, const LineLayout& line, const RunLayout& run, float xInRun) {
                (void)line;
                if (run.style == InlineStyle::Image) {
                    return run.textStart;
                }

                ConfigureDocumentFont(
                    renderContext.font,
                    typefaces,
                    block.type,
                    run.style,
                    appState.baseFontSize);

                if (xInRun <= 0.0f) {
                    return run.textStart;
                }

                float bestDistance = std::numeric_limits<float>::max();
                size_t bestOffset = run.text.size();
                for (size_t offset = 0; offset <= run.text.size();) {
                    const float width = renderContext.font.measureText(run.text.c_str(), offset, SkTextEncoding::kUTF8);
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
            },
        });

    hit.position = hitTest.position;
    hit.valid = hitTest.valid;
    hit.url = hitTest.url;
    hit.style = hitTest.style;
    return hit;
}

InteractionTextHit ToInteractionHit(const DocumentTextHit& hit) {
    return InteractionTextHit{
        .position = hit.position,
        .valid = hit.valid,
        .url = hit.url,
    };
}

void UpdateSelectionFromPoint(HWND hwnd, ViewerInteractionContext& context, int x, int y) {
    AppState& appState = GetAppState(context.host);
    UpdateSelectionFromHit(
        appState,
        ToInteractionHit(HitTestText(context, static_cast<float>(x), static_cast<float>(y))),
        static_cast<float>(y) - GetContentTopInset(),
        GetViewportHeight(hwnd, context.host),
        GetMaxScroll(hwnd, context.host));
}

void UpdateScrollOffsetFromThumb(HWND hwnd, ViewerInteractionContext& context, int mouseY) {
    AppState& appState = GetAppState(context.host);
    const auto thumb = GetScrollbarThumbRect(hwnd, context.host);
    if (!thumb) {
        return;
    }

    mdviewer::UpdateScrollOffsetFromThumb(
        appState,
        mouseY,
        *thumb,
        GetViewportHeight(hwnd, context.host),
        GetMaxScroll(hwnd, context.host),
        kScrollbarMargin);
}

bool CopySelection(HWND hwnd, ViewerInteractionContext& context) {
    AppState& appState = GetAppState(context.host);
    if (!appState.HasSelection()) {
        return false;
    }

    const size_t selectionStart = GetSelectionStart(appState);
    const size_t selectionEnd = GetSelectionEnd(appState);
    if (selectionEnd > appState.docLayout.plainText.size() || selectionStart >= selectionEnd) {
        return false;
    }

    return CopyUtf8TextToClipboard(
        hwnd,
        appState.docLayout.plainText.substr(selectionStart, selectionEnd - selectionStart));
}

InteractionKey TranslateInteractionKey(WPARAM wParam) {
    switch (wParam) {
        case VK_ESCAPE:
            return InteractionKey::Escape;
        case VK_OEM_PLUS:
        case VK_ADD:
            return InteractionKey::ZoomIn;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            return InteractionKey::ZoomOut;
        case VK_LEFT:
            return InteractionKey::Left;
        case VK_RIGHT:
            return InteractionKey::Right;
        case VK_BACK:
            return InteractionKey::Back;
        case 'C':
        case 'c':
            return InteractionKey::Copy;
        default:
            return InteractionKey::Unknown;
    }
}

bool ExecuteKeyCommand(HWND hwnd, ViewerInteractionContext& context, const KeyCommandResult& command) {
    if (command.stopAutoScroll) {
        StopAutoScroll(hwnd, context);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (command.zoomIn) {
        AdjustBaseFontSize(hwnd, context.host, 1.0f);
    }
    if (command.zoomOut) {
        AdjustBaseFontSize(hwnd, context.host, -1.0f);
    }
    if (command.goBack) {
        GoBack(hwnd, context.host);
    }
    if (command.goForward) {
        GoForward(hwnd, context.host);
    }
    if (command.copySelection) {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        CopySelection(hwnd, context);
    }
    return command.handled;
}

void StartAutoScroll(HWND hwnd, ViewerInteractionContext& context, int x, int y) {
    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        StartAutoScrollState(GetAppState(context.host), static_cast<float>(x), static_cast<float>(y));
    }
    SetCapture(hwnd);
    SetTimer(hwnd, context.autoScrollTimerId, context.autoScrollTimerMs, nullptr);
}

bool TickAutoScroll(HWND hwnd, ViewerInteractionContext& context) {
    std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
    return mdviewer::TickAutoScroll(
        GetAppState(context.host),
        GetMaxScroll(hwnd, context.host),
        context.autoScrollDeadZone);
}

} // namespace

void StopAutoScroll(HWND hwnd, ViewerInteractionContext& context) {
    const bool shouldReleaseCapture = GetCapture() == hwnd;
    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        StopAutoScrollState(GetAppState(context.host));
    }
    KillTimer(hwnd, context.autoScrollTimerId);
    if (shouldReleaseCapture) {
        ReleaseCapture();
    }
}

bool HandleDropFiles(HWND hwnd, ViewerInteractionContext& context, HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0) {
        WCHAR path[MAX_PATH] = {};
        if (DragQueryFileW(drop, 0, path, MAX_PATH)) {
            LoadFile(hwnd, context.host, path);
        }
    }
    DragFinish(drop);
    return true;
}

bool HandlePrimaryButtonDown(HWND hwnd, ViewerInteractionContext& context, int x, int y) {
    if (!context.host.surface) {
        return false;
    }

    const int topMenuIndex = HitTestTopMenu(hwnd, x, y, context.host.surface->width());
    if (topMenuIndex != -1) {
        StopAutoScroll(hwnd, context);
        SetFocus(hwnd);
        if (const UINT command = OpenTopMenu(hwnd, topMenuIndex); command != 0) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
        return true;
    }

    const auto hit = HitTestText(context, static_cast<float>(x), static_cast<float>(y));

    {
        std::lock_guard<std::mutex> buttonLock(GetAppState(context.host).mtx);
        AppState& appState = GetAppState(context.host);
        const float docX = static_cast<float>(x);
        const float docY = static_cast<float>(y) - GetContentTopInset() + appState.scrollOffset;
        for (const auto& btn : appState.codeBlockButtons) {
            if (!btn.first.contains(docX, docY)) {
                continue;
            }

            const size_t start = btn.second.first;
            const size_t end = btn.second.second;
            if (end > start && end <= appState.docLayout.plainText.size()) {
                CopyUtf8TextToClipboard(hwnd, appState.docLayout.plainText.substr(start, end - start));
                appState.copiedFeedbackTimeout = GetTickCount64() + 2000;
                SetTimer(hwnd, context.copiedFeedbackTimerId, 2000, nullptr);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
    }

    StopAutoScroll(hwnd, context);
    SetFocus(hwnd);
    SetCapture(hwnd);
    if (const auto thumb = GetScrollbarThumbRect(hwnd, context.host);
        thumb && thumb->contains(static_cast<float>(x), static_cast<float>(y))) {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        BeginScrollbarDrag(GetAppState(context.host), static_cast<float>(y) - thumb->top());
    } else if (
        x >= context.host.surface->width() - static_cast<int>(kScrollbarWidth + (kScrollbarMargin * 2.0f)) &&
        GetScrollbarThumbRect(hwnd, context.host)) {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        BeginScrollbarDrag(GetAppState(context.host), GetScrollbarThumbRect(hwnd, context.host)->height() * 0.5f);
        UpdateScrollOffsetFromThumb(hwnd, context, y);
    } else {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        BeginSelection(
            GetAppState(context.host),
            ToInteractionHit(hit),
            (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            x,
            y);
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool HandlePointerMove(HWND hwnd, ViewerInteractionContext& context, WPARAM mouseButtons, int x, int y) {
    if (!context.host.surface) {
        return false;
    }

    if (UpdateTopMenuHover(hwnd, x, y, context.host.surface->width())) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    {
        auto hit = HitTestText(context, static_cast<float>(x), static_cast<float>(y));
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        if (UpdateHoveredUrl(GetAppState(context.host), ToInteractionHit(hit))) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    bool isAutoScrolling = false;
    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        isAutoScrolling = GetAppState(context.host).isAutoScrolling;
        if (isAutoScrolling) {
            UpdateAutoScrollCursor(GetAppState(context.host), static_cast<float>(x), static_cast<float>(y));
        }
    }
    if (isAutoScrolling) {
        TickAutoScroll(hwnd, context);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    if ((mouseButtons & MK_LBUTTON) == 0) {
        return true;
    }

    std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
    if (GetAppState(context.host).isDraggingScrollbar) {
        UpdateScrollOffsetFromThumb(hwnd, context, y);
        ClampScrollOffset(hwnd, context.host);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (GetAppState(context.host).isSelecting) {
        if (!CancelPendingLinkIfDragged(GetAppState(context.host), x, y, context.linkClickSlop) &&
            GetAppState(context.host).pendingLinkClick) {
            return true;
        }
        UpdateSelectionFromPoint(hwnd, context, x, y);
        ClampScrollOffset(hwnd, context.host);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return true;
}

bool HandlePrimaryButtonUp(HWND hwnd, ViewerInteractionContext& context, int x, int y) {
    const auto releaseHit = HitTestText(context, static_cast<float>(x), static_cast<float>(y));

    bool shouldUpdateSelection = false;
    bool activateLink = false;
    bool forceExternal = false;
    std::string linkUrl;

    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        const auto result = FinishPrimaryPointerInteraction(GetAppState(context.host), ToInteractionHit(releaseHit));
        shouldUpdateSelection = result.shouldUpdateSelection;
        activateLink = result.activateLink;
        forceExternal = result.forceExternal;
        linkUrl = result.linkUrl;
    }

    ReleaseCapture();

    if (shouldUpdateSelection) {
        UpdateSelectionFromPoint(hwnd, context, x, y);
    }

    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        FinalizeSelectionInteraction(GetAppState(context.host));
    }

    if (activateLink) {
        HandleLinkClick(hwnd, context.host, linkUrl, forceExternal);
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool HandleContextMenu(HWND hwnd, ViewerInteractionContext& context, int screenX, int screenY) {
    if (!context.host.surface) {
        return false;
    }

    POINT screenPoint{screenX, screenY};
    if (screenPoint.x == -1 && screenPoint.y == -1) {
        GetCursorPos(&screenPoint);
    }

    POINT clientPoint = screenPoint;
    ScreenToClient(hwnd, &clientPoint);
    if (clientPoint.y < GetContentTopInset()) {
        return false;
    }

    StopAutoScroll(hwnd, context);
    SetFocus(hwnd);

    const auto hit = HitTestText(context, static_cast<float>(clientPoint.x), static_cast<float>(clientPoint.y));
    DocumentContextMenu menu;
    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        menu = BuildDocumentContextMenu(GetAppState(context.host), ToInteractionHit(hit));
    }

    const auto command = ShowDocumentContextMenu(hwnd, menu, screenPoint);
    if (!command) {
        return true;
    }

    switch (*command) {
        case DocumentContextCommand::CopySelection: {
            std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
            CopySelection(hwnd, context);
            break;
        }
        case DocumentContextCommand::OpenLink:
            HandleLinkClick(hwnd, context.host, menu.linkUrl, false);
            break;
        case DocumentContextCommand::CopyLink:
            CopyUtf8TextToClipboard(hwnd, menu.linkUrl);
            break;
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool HandleMiddleButtonDown(HWND hwnd, ViewerInteractionContext& context, int x, int y) {
    if (y < kMenuBarHeight) {
        return true;
    }

    SetFocus(hwnd);
    if (GetAppState(context.host).isAutoScrolling) {
        StopAutoScroll(hwnd, context);
    } else {
        StartAutoScroll(hwnd, context, x, y);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool HandleXButtonDown(HWND hwnd, ViewerInteractionContext& context, WPARAM wParam) {
    if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
        GoBack(hwnd, context.host);
    } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
        GoForward(hwnd, context.host);
    }
    return true;
}

bool HandleMouseWheel(HWND hwnd, ViewerInteractionContext& context, int delta) {
    {
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        ApplyWheelScroll(GetAppState(context.host), static_cast<float>(delta), GetMaxScroll(hwnd, context.host));
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool HandleKeyDown(HWND hwnd, ViewerInteractionContext& context, WPARAM wParam) {
    return ExecuteKeyCommand(
        hwnd,
        context,
        mdviewer::HandleKeyDown(
            GetAppState(context.host),
            KeyEvent{
                .key = TranslateInteractionKey(wParam),
                .ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                .alt = (GetKeyState(VK_MENU) & 0x8000) != 0,
            }));
}

bool HandleTimer(HWND hwnd, ViewerInteractionContext& context, WPARAM timerId) {
    if (timerId == context.autoScrollTimerId) {
        if (TickAutoScroll(hwnd, context)) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }

    if (timerId == context.copiedFeedbackTimerId) {
        {
            std::lock_guard<std::mutex> timerLock(GetAppState(context.host).mtx);
            GetAppState(context.host).copiedFeedbackTimeout = 0;
        }
        KillTimer(hwnd, context.copiedFeedbackTimerId);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    return false;
}

bool HandleCaptureChanged(HWND hwnd, ViewerInteractionContext& context, LPARAM capturedWindow) {
    if (reinterpret_cast<HWND>(capturedWindow) != hwnd) {
        StopAutoScroll(hwnd, context);
        std::lock_guard<std::mutex> lock(GetAppState(context.host).mtx);
        ClearPendingLinkState(GetAppState(context.host));
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return true;
}

} // namespace mdviewer::win
