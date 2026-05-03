#include "platform/win/win_viewer_host.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

#include "app/heading_anchor.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_file_watcher.h"
#include "platform/win/win_shell.h"
#include "platform/win/win_surface.h"
#include "view/document_interaction.h"
#include "view/document_outline.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#pragma warning(pop)

namespace mdviewer::win {

namespace {

constexpr uint64_t kZoomFeedbackDurationMs = 1200;

AppState& State(const ViewerHostContext& context) {
    return context.controller.GetMutableAppState();
}

std::filesystem::path NormalizePathForCompare(const std::filesystem::path& path) {
    try {
        return std::filesystem::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return NormalizePathForCompare(left) == NormalizePathForCompare(right);
}

bool ScrollToFragment(HWND hwnd, ViewerHostContext& context, const std::string& fragment) {
    if (fragment.empty()) {
        return false;
    }

    AppState& appState = State(context);
    const auto it = appState.docLayout.anchors.find(fragment);
    const auto normalizedIt = it == appState.docLayout.anchors.end()
        ? appState.docLayout.anchors.find(MakeHeadingAnchor(fragment))
        : it;
    if (normalizedIt == appState.docLayout.anchors.end()) {
        return false;
    }

    appState.scrollOffset = normalizedIt->second;
    ClearRelayoutScrollAnchor(appState);
    ClampScrollOffset(hwnd, context);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

} // namespace

AppState& GetAppState(const ViewerHostContext& context) {
    return State(context);
}

ThemePalette GetCurrentThemePalette(const ViewerHostContext& context) {
    return GetThemePalette(context.controller.GetTheme());
}

bool CanZoomIn(const ViewerHostContext& context) {
    return context.controller.CanZoomIn();
}

bool CanZoomOut(const ViewerHostContext& context) {
    return context.controller.CanZoomOut();
}

bool EnsureFontSystem(ViewerHostContext& context) {
    return context.typefaces.EnsureInitialized(context.controller.GetFontFamilyUtf8());
}

DocumentTypefaceSet GetDocumentTypefaces(ViewerHostContext& context) {
    return context.typefaces.GetTypefaceSet();
}

SkTypeface* GetRegularTypeface(ViewerHostContext& context) {
    return context.typefaces.GetRegularTypeface();
}

SkTypeface* GetMenuTypeface(ViewerHostContext& context) {
    return context.typefaces.GetUiTypeface();
}

float GetContentTopInset() {
    return static_cast<float>(kMenuBarHeight);
}

float GetDocumentLeftInset(const ViewerHostContext& context) {
    return GetOutlineSidebarWidth(State(context));
}

float GetDocumentLayoutWidth(HWND hwnd, const ViewerHostContext& context) {
    RECT rect = {};
    GetClientRect(hwnd, &rect);
    return std::max(static_cast<float>(rect.right - rect.left) - GetDocumentLeftInset(context), 1.0f);
}

float GetViewportHeight(HWND hwnd, const ViewerHostContext& context) {
    (void)context;
    RECT rect = {};
    GetClientRect(hwnd, &rect);
    return std::max(static_cast<float>(rect.bottom - rect.top) - GetContentTopInset(), 0.0f);
}

float GetMaxScroll(HWND hwnd, const ViewerHostContext& context) {
    return std::max(State(context).docLayout.totalHeight - GetViewportHeight(hwnd, context), 0.0f);
}

void ClampScrollOffset(HWND hwnd, const ViewerHostContext& context) {
    AppState& appState = State(context);
    appState.scrollOffset = std::clamp(appState.scrollOffset, 0.0f, GetMaxScroll(hwnd, context));
}

void SyncMenuState(HWND hwnd, const ViewerHostContext& context) {
    UpdateMenuState(
        hwnd,
        context.controller.GetTheme(),
        context.controller.HasCustomFontFamily(),
        GetCurrentThemePalette(context),
        context.controller.GetRecentFiles());
}

void UpdateSurface(HWND hwnd, ViewerHostContext& context) {
    EnsureRasterSurfaceSize(hwnd, context.surface);
}

std::optional<SkRect> GetScrollbarThumbRect(HWND hwnd, const ViewerHostContext& context) {
    const AppState& appState = State(context);
    const float viewportHeight = GetViewportHeight(hwnd, context);
    if (!context.surface || appState.docLayout.totalHeight <= viewportHeight || viewportHeight <= 0.0f) {
        return std::nullopt;
    }

    const float scrollRatio = viewportHeight / appState.docLayout.totalHeight;
    const float thumbHeight = std::max(viewportHeight * scrollRatio, 24.0f);
    const float maxThumbTravel = viewportHeight - thumbHeight;
    const float maxScroll = GetMaxScroll(hwnd, context);
    const float thumbY = maxScroll > 0.0f ? (appState.scrollOffset / maxScroll) * maxThumbTravel : 0.0f;

    return SkRect::MakeXYWH(
        context.surface->width() - kScrollbarWidth - kScrollbarMargin,
        thumbY + kScrollbarMargin + GetContentTopInset(),
        kScrollbarWidth,
        thumbHeight);
}

void Render(HWND hwnd, ViewerHostContext& context) {
    if (!context.surface) {
        return;
    }

    if (!EnsureFontSystem(context)) {
        PAINTSTRUCT ps = {};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    AppState& appState = State(context);
    SkCanvas* canvas = context.surface->getCanvas();
    const ThemePalette palette = GetCurrentThemePalette(context);
    canvas->clear(palette.windowBackground);

    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        appState.codeBlockButtons.clear();
        RenderDocumentScene(
            DocumentSceneParams{
                .canvas = canvas,
                .appState = &appState,
                .palette = palette,
                .typefaces = GetDocumentTypefaces(context),
                .baseFontSize = appState.baseFontSize,
                .contentTopInset = GetContentTopInset(),
                .viewportHeight = GetViewportHeight(hwnd, context),
                .surfaceWidth = static_cast<float>(context.surface->width()),
                .surfaceHeight = static_cast<float>(context.surface->height()),
                .documentLeftInset = GetDocumentLeftInset(context),
                .scrollbarWidth = kScrollbarWidth,
                .scrollbarMargin = kScrollbarMargin,
                .currentTickCount = GetTickCount64(),
                .visibleDocumentTop = appState.scrollOffset,
                .visibleDocumentBottom = appState.scrollOffset + GetViewportHeight(hwnd, context),
                .scrollbarThumbRect = GetScrollbarThumbRect(hwnd, context),
                .resolveImage = [&](const std::string& url, float displayWidth, float displayHeight) -> sk_sp<SkImage> {
                    return context.imageCache.GetImage(
                        url,
                        appState.currentFilePath.parent_path(),
                        displayWidth,
                        displayHeight);
                },
                .addCodeBlockButton = [&](const SkRect& rect, size_t start, size_t end) {
                    appState.codeBlockButtons.push_back({rect, {start, end}});
                },
            });
    }

    DrawTopMenuBar(
        canvas,
        hwnd,
        context.surface->width(),
        GetMenuTypeface(context),
        palette,
        appState.CanGoBack(),
        appState.CanGoForward(),
        CanZoomOut(context),
        CanZoomIn(context));

    PresentRasterSurface(hwnd, context.surface.get());
}

bool LoadFile(HWND hwnd, ViewerHostContext& context, const std::filesystem::path& path, bool pushHistory) {
    if (!EnsureFontSystem(context)) {
        MessageBoxW(hwnd, L"Font initialization failed. The document cannot be rendered.", L"Error", MB_ICONERROR);
        return false;
    }

    RECT rect = {};
    GetClientRect(hwnd, &rect);
    const float width = std::max(static_cast<float>(rect.right - rect.left), 1.0f);
    const std::filesystem::path baseDir = path.parent_path();

    auto imageSizeProvider = [&](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };

    const auto status = context.controller.OpenFile(
        path,
        width,
        GetRegularTypeface(context),
        [&](const DocumentModel& docModel, const std::filesystem::path& preloadBaseDir) {
            context.imageCache.PreloadDocumentImages(docModel, preloadBaseDir);
        },
        imageSizeProvider,
        pushHistory);

    if (status == OpenDocumentStatus::BinaryFile) {
        MessageBoxW(hwnd, L"The file appears to be a binary file and cannot be opened internally.", L"Information", MB_ICONINFORMATION);
        return false;
    }
    if (status == OpenDocumentStatus::FileReadError) {
        MessageBoxW(hwnd, (L"Could not load file: " + path.wstring()).c_str(), L"Error", MB_ICONERROR);
        return false;
    }

    const std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
    SetWindowTextW(hwnd, title.c_str());
    context.fileWatcher.SetWatchedFile(path);
    context.controller.SaveConfig();
    SyncMenuState(hwnd, context);

    if (!State(context).docLayout.outline.empty()) {
        RelayoutCurrentDocument(hwnd, context);
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void GoBack(HWND hwnd, ViewerHostContext& context) {
    if (const auto target = context.controller.GetHistoryNavigationTarget(HistoryDirection::Back)) {
        if (LoadFile(hwnd, context, target->path, false)) {
            context.controller.CommitHistoryNavigation(target->index);
        }
    }
}

void GoForward(HWND hwnd, ViewerHostContext& context) {
    if (const auto target = context.controller.GetHistoryNavigationTarget(HistoryDirection::Forward)) {
        if (LoadFile(hwnd, context, target->path, false)) {
            context.controller.CommitHistoryNavigation(target->index);
        }
    }
}

void HandleLinkClick(HWND hwnd, ViewerHostContext& context, const std::string& url, bool forceExternal) {
    if (url.empty()) {
        return;
    }

    const auto target = context.controller.ResolveLinkTarget(url, forceExternal);
    switch (target.kind) {
        case LinkTargetKind::InternalDocument:
            if (SamePath(target.path, State(context).currentFilePath)) {
                ScrollToFragment(hwnd, context, target.fragment);
            } else if (LoadFile(hwnd, context, target.path)) {
                ScrollToFragment(hwnd, context, target.fragment);
            }
            break;
        case LinkTargetKind::ExternalUrl:
            OpenExternalUrl(target.externalUrl);
            break;
        case LinkTargetKind::ExternalPath:
            OpenPath(target.path);
            break;
        case LinkTargetKind::Invalid:
        default:
            break;
    }
}

void RelayoutCurrentDocument(HWND hwnd, ViewerHostContext& context) {
    if (!EnsureFontSystem(context)) {
        return;
    }

    AppState& appState = State(context);
    const float viewportHeight = GetViewportHeight(hwnd, context);
    const ScrollAnchor scrollAnchor = GetRelayoutScrollAnchor(appState, viewportHeight);

    RECT rect = {};
    GetClientRect(hwnd, &rect);
    const float width = GetDocumentLayoutWidth(hwnd, context);
    const std::filesystem::path baseDir = appState.currentFilePath.parent_path();

    auto imageSizeProvider = [&](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };

    if (!context.controller.Relayout(
            width,
            GetRegularTypeface(context),
            [&](const DocumentModel& docModel, const std::filesystem::path& preloadBaseDir) {
                context.imageCache.PreloadDocumentImages(docModel, preloadBaseDir);
            },
            imageSizeProvider)) {
        return;
    }

    RestoreScrollAnchor(appState, scrollAnchor, GetViewportHeight(hwnd, context), GetMaxScroll(hwnd, context));
    RememberRelayoutScrollAnchor(appState, scrollAnchor);
}

bool ReloadCurrentFile(HWND hwnd, ViewerHostContext& context, bool preserveScrollOffset) {
    if (!EnsureFontSystem(context)) {
        return false;
    }

    RECT rect = {};
    GetClientRect(hwnd, &rect);
    const float width = GetDocumentLayoutWidth(hwnd, context);
    const std::filesystem::path currentPath = State(context).currentFilePath;
    const std::filesystem::path baseDir = currentPath.parent_path();
    const float viewportHeight = GetViewportHeight(hwnd, context);
    const ScrollAnchor scrollAnchor = GetRelayoutScrollAnchor(State(context), viewportHeight);

    const auto status = context.controller.ReloadCurrentFile(
        width,
        GetRegularTypeface(context),
        [&](const DocumentModel& docModel, const std::filesystem::path& preloadBaseDir) {
            context.imageCache.PreloadDocumentImages(docModel, preloadBaseDir);
        },
        [&](const std::string& url) -> std::pair<float, float> {
            return context.imageCache.GetImageSize(url, baseDir);
        });
    if (status != OpenDocumentStatus::Success) {
        return false;
    }

    if (preserveScrollOffset) {
        RestoreScrollAnchor(State(context), scrollAnchor, GetViewportHeight(hwnd, context), GetMaxScroll(hwnd, context));
        RememberRelayoutScrollAnchor(State(context), scrollAnchor);
    } else {
        ClearRelayoutScrollAnchor(State(context));
    }

    context.fileWatcher.SetWatchedFile(currentPath);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void ReloadIfFileChanged(HWND hwnd, ViewerHostContext& context) {
    if (!context.controller.HasCurrentFileChanged()) {
        return;
    }

    if (!ReloadCurrentFile(hwnd, context, true)) {
        context.controller.SyncCurrentFileWriteTime();
    }
}

void SetBaseFontSize(HWND hwnd, ViewerHostContext& context, float baseFontSize) {
    if (!context.controller.SetBaseFontSize(baseFontSize)) {
        return;
    }

    State(context).zoomFeedbackFontSize = context.controller.GetBaseFontSize();
    State(context).zoomFeedbackTimeout = GetTickCount64() + kZoomFeedbackDurationMs;
    RelayoutCurrentDocument(hwnd, context);
    context.controller.SaveConfig();
    SyncMenuState(hwnd, context);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void AdjustBaseFontSize(HWND hwnd, ViewerHostContext& context, float delta) {
    SetBaseFontSize(hwnd, context, context.controller.GetBaseFontSize() + delta);
}

void ApplySelectedFont(HWND hwnd, ViewerHostContext& context, const std::string& familyUtf8) {
    const std::string previousFamily = context.controller.GetFontFamilyUtf8();
    context.controller.SetFontFamilyUtf8(familyUtf8);

    if (!EnsureFontSystem(context)) {
        context.controller.SetFontFamilyUtf8(previousFamily);
        EnsureFontSystem(context);
        MessageBoxW(hwnd, L"The selected font could not be loaded.", L"Error", MB_ICONERROR);
        return;
    }

    RelayoutCurrentDocument(hwnd, context);
    context.controller.SaveConfig();
    SyncMenuState(hwnd, context);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ApplyTheme(HWND hwnd, ViewerHostContext& context, ThemeMode theme) {
    if (!context.controller.SetTheme(theme)) {
        return;
    }

    context.controller.SaveConfig();
    SyncMenuState(hwnd, context);
    InvalidateRect(hwnd, nullptr, FALSE);
}

} // namespace mdviewer::win
