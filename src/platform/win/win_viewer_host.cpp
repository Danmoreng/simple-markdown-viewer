#include "platform/win/win_viewer_host.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

#include <commdlg.h>

#include "app/heading_anchor.h"
#include "platform/win/win_menu.h"
#include "platform/win/win_file_watcher.h"
#include "platform/win/win_message_dialog.h"
#include "platform/win/win_shell.h"
#include "platform/win/win_surface.h"
#include "platform/win/win_dpi.h"
#include "render/pdf_exporter.h"
#include "render/print_document.h"
#include "view/document_interaction.h"
#include "view/document_outline.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"
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

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

void ShowMissingLocalPath(HWND hwnd, const std::filesystem::path& path) {
    ShowErrorMessage(
        hwnd,
        "Link not found",
        "The linked file or folder could not be found:\n\n" + PathToUtf8(path));
}

void ShowMissingFragment(HWND hwnd, const std::string& fragment) {
    ShowErrorMessage(
        hwnd,
        "Section not found",
        "The linked section could not be found in the document:\n\n#" + fragment);
}

bool ConfirmLocalLinkTarget(HWND hwnd, const LinkTarget& target) {
    std::string message;
    if (target.executableLocalFile) {
        message += "This link points to a file that may execute code.\n";
    }
    message += "\n" + PathToUtf8(target.path) + "\n\nOpen it anyway?";
    return ConfirmWarning(hwnd, "Open local target?", message);
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
    return std::max(LogicalClientWidth(hwnd) - GetDocumentLeftInset(context), 1.0f);
}

float GetLogicalSurfaceWidth(const ViewerHostContext& context) {
    return context.surface ? PixelsToLogical(static_cast<float>(context.surface->width()), context.dpiScale) : 0.0f;
}

float GetLogicalSurfaceHeight(const ViewerHostContext& context) {
    return context.surface ? PixelsToLogical(static_cast<float>(context.surface->height()), context.dpiScale) : 0.0f;
}

float GetViewportHeight(HWND hwnd, const ViewerHostContext& context) {
    (void)context;
    return std::max(LogicalClientHeight(hwnd) - GetContentTopInset(), 0.0f);
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
        context.controller.HasCurrentFile(),
        State(context).outlineCollapsed,
        context.controller.GetOutlineSide(),
        context.controller.HasCustomFontFamily(),
        GetCurrentThemePalette(context),
        context.controller.GetRecentFiles());
}

void UpdateSurface(HWND hwnd, ViewerHostContext& context) {
    context.dpiScale = WindowDpiScale(hwnd);
    context.surface.EnsureSize(hwnd);
}

void ProcessWindowResize(HWND hwnd, ViewerHostContext& context) {
    context.windowResizePending = false;
    if (IsIconic(hwnd)) return;
    UpdateSurface(hwnd, context);
    if (LayoutWidthChanged(context.lastLayoutWidth, GetDocumentLayoutWidth(hwnd, context))) {
        RelayoutCurrentDocument(hwnd, context);
    } else {
        ClampScrollOffset(hwnd, context);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
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
    const float thumbX = appState.outlineSide == OutlineSide::Right
        ? GetLogicalSurfaceWidth(context) - GetDocumentLeftInset(context) - kScrollbarWidth - kScrollbarMargin
        : GetLogicalSurfaceWidth(context) - kScrollbarWidth - kScrollbarMargin;

    return SkRect::MakeXYWH(
        thumbX,
        thumbY + kScrollbarMargin + GetContentTopInset(),
        kScrollbarWidth,
        thumbHeight);
}

void Render(HWND hwnd, ViewerHostContext& context) {
    AppState& appState = State(context);
    bool interactiveResize = false;
    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        interactiveResize = appState.isResizingOutline || context.liveWindowResize;
    }

    if (!context.surface.BeginFrame(hwnd, interactiveResize)) {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    if (!EnsureFontSystem(context)) {
        PAINTSTRUCT ps = {};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return;
    }

    SkCanvas* canvas = context.surface->getCanvas();
    context.dpiScale = WindowDpiScale(hwnd);
    SkAutoCanvasRestore canvasRestore(canvas, true);
    canvas->scale(context.dpiScale, context.dpiScale);
    const ThemePalette palette = GetCurrentThemePalette(context);
    canvas->clear(palette.windowBackground);

    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        appState.codeBlockButtons.clear();
        appState.imageLoadButtons.clear();
        appState.horizontalScrollbars.clear();
        SyncOutlineScrollToDocument(
            appState,
            GetLogicalSurfaceHeight(context),
            GetContentTopInset());
        RenderDocumentScene(
            DocumentSceneParams{
                .canvas = canvas,
                .appState = &appState,
                .palette = palette,
                .typefaces = GetDocumentTypefaces(context),
                .baseFontSize = appState.baseFontSize,
                .contentTopInset = GetContentTopInset(),
                .viewportHeight = GetViewportHeight(hwnd, context),
                .surfaceWidth = GetLogicalSurfaceWidth(context),
                .surfaceHeight = GetLogicalSurfaceHeight(context),
                .documentLeftInset = GetDocumentLeftInset(context),
                .scrollbarWidth = kScrollbarWidth,
                .scrollbarMargin = kScrollbarMargin,
                .currentTickCount = GetTickCount64(),
                .visibleDocumentTop = appState.scrollOffset,
                .visibleDocumentBottom = appState.scrollOffset + GetViewportHeight(hwnd, context),
                .freezeImageDimensions = context.imageCache.IsLiveResize(),
                .scrollbarThumbRect = GetScrollbarThumbRect(hwnd, context),
                .resolveImage = [&](const std::string& url, float displayWidth, float displayHeight) -> sk_sp<SkImage> {
                    return context.imageCache.GetImage(
                        url,
                        appState.currentFilePath.parent_path(),
                        displayWidth * context.dpiScale,
                        displayHeight * context.dpiScale);
                },
                .addCodeBlockButton = [&](const SkRect& rect, size_t start, size_t end) {
                    appState.codeBlockButtons.push_back({rect, {start, end}});
                },
                .addHorizontalScrollbar = [&](const HorizontalScrollbarRegion& region) {
                    appState.horizontalScrollbars.push_back(region);
                },
                .imageLoadInfoProvider = [&](const std::string& source) {
                    return context.imageCache.GetSourceInfo(source, appState.currentFilePath.parent_path());
                },
                .addImageLoadButton = [&](const ImageLoadButtonRegion& region) {
                    appState.imageLoadButtons.push_back(region);
                },
            });
    }

    DrawTopMenuBar(
        canvas,
        hwnd,
        static_cast<int>(std::lround(GetLogicalSurfaceWidth(context))),
        GetMenuTypeface(context),
        palette,
        appState.CanGoBack(),
        appState.CanGoForward(),
        CanZoomOut(context),
        CanZoomIn(context));

    canvasRestore.restore();
    context.surface.Present(hwnd, interactiveResize);
}

bool LoadFile(HWND hwnd, ViewerHostContext& context, const std::filesystem::path& path, bool pushHistory) {
    context.controller.CancelRelayout();
    KillTimer(hwnd, kRelayoutPollTimerId);
    if (!EnsureFontSystem(context)) {
        MessageBoxW(hwnd, L"Font initialization failed. The document cannot be rendered.", L"Error", MB_ICONERROR);
        return false;
    }

    const float width = std::max(LogicalClientWidth(hwnd), 1.0f);
    const std::filesystem::path baseDir = path.parent_path();

    auto imageSizeProvider = [&](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };

    context.imageCache.Clear();
    const auto status = context.controller.OpenFile(
        path,
        width,
        GetDocumentTypefaces(context),
        [&](const DocumentModel& docModel, const std::filesystem::path& preloadBaseDir) {
            context.imageCache.PreloadDocumentImages(docModel, preloadBaseDir);
        },
        imageSizeProvider,
        pushHistory,
        true,
        LayoutOptions{.imageLoadInfoProvider = [&](const std::string& source) {
            return context.imageCache.GetSourceInfo(source, baseDir);
        }});

    if (status == OpenDocumentStatus::BinaryFile) {
        MessageBoxW(hwnd, L"The file appears to be a binary file and cannot be opened internally.", L"Information", MB_ICONINFORMATION);
        return false;
    }
    if (status == OpenDocumentStatus::FileReadError) {
        MessageBoxW(hwnd, (L"Could not load file: " + path.wstring()).c_str(), L"Error", MB_ICONERROR);
        return false;
    }
    if (status == OpenDocumentStatus::FileTooLarge) {
        MessageBoxW(hwnd, L"The file exceeds the current 64 MiB safety limit.", L"File too large", MB_ICONWARNING);
        return false;
    }

    const std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
    SetWindowTextW(hwnd, title.c_str());
    context.fileWatcher.SetWatchedFile(path);
    context.lastLayoutWidth = width;
    context.controller.SaveConfig();
    SyncMenuState(hwnd, context);

    if (!State(context).docLayout.outline.empty()) {
        RelayoutCurrentDocument(hwnd, context);
    }
    ClampScrollOffset(hwnd, context);

    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool SaveCurrentDocumentAsPdf(HWND hwnd, ViewerHostContext& context, const std::filesystem::path& outputPath) {
    if (!EnsureFontSystem(context)) {
        MessageBoxW(hwnd, L"Font initialization failed. The PDF cannot be rendered.", L"Save as PDF", MB_ICONERROR);
        return false;
    }

    PdfExportRequest request;
    request.outputPath = outputPath;
    request.typefaces = GetDocumentTypefaces(context);

    AppState& appState = State(context);
    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        request.sourcePath = appState.currentFilePath;
        request.sourceText = appState.sourceText;
        request.document = appState.docModel;
        request.theme = appState.theme;
        request.baseFontSize = appState.baseFontSize;
    }

    if (request.sourcePath.empty()) {
        MessageBoxW(hwnd, L"Open a Markdown file before saving as PDF.", L"Save as PDF", MB_ICONINFORMATION);
        return false;
    }

    const std::filesystem::path baseDir = request.sourcePath.parent_path();
    request.imageSizeProvider = [&](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };
    request.resolveImage = [&](const std::string& url, float displayWidth, float displayHeight) -> sk_sp<SkImage> {
        return context.imageCache.GetImage(url, baseDir, displayWidth, displayHeight);
    };

    const PdfExportStatus status = ExportMarkdownToPdf(request);
    if (status == PdfExportStatus::Success) {
        return true;
    }

    std::wstring message;
    switch (status) {
        case PdfExportStatus::PdfBackendUnavailable:
            message = L"PDF export is unavailable because Skia was built without PDF support. Rebuild Skia with the updated build script.";
            break;
        case PdfExportStatus::NoDocument:
            message = L"Open a Markdown file before saving as PDF.";
            break;
        case PdfExportStatus::FileOpenError:
            message = L"Could not open the PDF file for writing.";
            break;
        case PdfExportStatus::WriteError:
        default:
            message = L"Could not write the PDF file.";
            break;
    }
    MessageBoxW(hwnd, message.c_str(), L"Save as PDF", MB_ICONERROR);
    return false;
}

bool PrintCurrentDocument(HWND hwnd, ViewerHostContext& context) {
    if (!EnsureFontSystem(context)) {
        MessageBoxW(hwnd, L"Font initialization failed. The document cannot be printed.", L"Print", MB_ICONERROR);
        return false;
    }

    PrintDocumentRequest request;
    request.typefaces = GetDocumentTypefaces(context);

    AppState& appState = State(context);
    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        request.sourcePath = appState.currentFilePath;
        request.sourceText = appState.sourceText;
        request.document = appState.docModel;
        request.theme = appState.theme;
        request.baseFontSize = appState.baseFontSize;
    }

    if (request.sourcePath.empty()) {
        MessageBoxW(hwnd, L"Open a Markdown file before printing.", L"Print", MB_ICONINFORMATION);
        return false;
    }

    PRINTDLGW printDialog = {};
    printDialog.lStructSize = sizeof(printDialog);
    printDialog.hwndOwner = hwnd;
    printDialog.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;

    if (!PrintDlgW(&printDialog)) {
        return false;
    }

    HDC printerDc = printDialog.hDC;
    if (!printerDc) {
        MessageBoxW(hwnd, L"Could not open the selected printer.", L"Print", MB_ICONERROR);
        return false;
    }

    const int printableWidthPx = GetDeviceCaps(printerDc, HORZRES);
    const int printableHeightPx = GetDeviceCaps(printerDc, VERTRES);
    const int dpiX = std::max(GetDeviceCaps(printerDc, LOGPIXELSX), 1);
    const int dpiY = std::max(GetDeviceCaps(printerDc, LOGPIXELSY), 1);
    if (printableWidthPx <= 0 || printableHeightPx <= 0) {
        DeleteDC(printerDc);
        MessageBoxW(hwnd, L"The selected printer reported an invalid printable area.", L"Print", MB_ICONERROR);
        return false;
    }

    request.pageWidth = static_cast<float>(printableWidthPx) * 72.0f / static_cast<float>(dpiX);
    request.pageHeight = static_cast<float>(printableHeightPx) * 72.0f / static_cast<float>(dpiY);

    const std::filesystem::path baseDir = request.sourcePath.parent_path();
    request.imageSizeProvider = [&](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };

    PreparedPrintDocument prepared;
    if (!PreparePrintDocument(request, prepared)) {
        DeleteDC(printerDc);
        MessageBoxW(hwnd, L"Could not prepare the document for printing.", L"Print", MB_ICONERROR);
        return false;
    }

    const std::wstring documentName = request.sourcePath.filename().empty()
        ? L"Markdown document"
        : request.sourcePath.filename().wstring();
    DOCINFOW docInfo = {};
    docInfo.cbSize = sizeof(docInfo);
    docInfo.lpszDocName = documentName.c_str();

    if (StartDocW(printerDc, &docInfo) <= 0) {
        DeleteDC(printerDc);
        MessageBoxW(hwnd, L"Could not start the print job.", L"Print", MB_ICONERROR);
        return false;
    }

    bool success = true;
    for (size_t pageIndex = 0; pageIndex < prepared.pages.size(); ++pageIndex) {
        if (StartPage(printerDc) <= 0) {
            success = false;
            break;
        }

        sk_sp<SkSurface> pageSurface = SkSurfaces::Raster(
            SkImageInfo::MakeN32Premul(printableWidthPx, printableHeightPx));
        if (!pageSurface) {
            success = false;
            EndPage(printerDc);
            break;
        }

        SkCanvas* canvas = pageSurface->getCanvas();
        canvas->scale(static_cast<float>(dpiX) / 72.0f, static_cast<float>(dpiY) / 72.0f);
        RenderPrintDocumentPage(
            prepared,
            pageIndex,
            canvas,
            [&](const std::string& url, float displayWidth, float displayHeight) -> sk_sp<SkImage> {
                return context.imageCache.GetImage(url, baseDir, displayWidth, displayHeight);
            });
        SkPixmap pixmap;
        if (!pageSurface->peekPixels(&pixmap)) {
            success = false;
            EndPage(printerDc);
            break;
        }

        BITMAPINFO bitmapInfo = {};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = printableWidthPx;
        bitmapInfo.bmiHeader.biHeight = -printableHeightPx;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        const int scanLines = SetDIBitsToDevice(
            printerDc,
            0,
            0,
            printableWidthPx,
            printableHeightPx,
            0,
            0,
            0,
            static_cast<UINT>(printableHeightPx),
            pixmap.addr(),
            &bitmapInfo,
            DIB_RGB_COLORS);
        if (scanLines == 0 || EndPage(printerDc) <= 0) {
            success = false;
            break;
        }
    }

    if (success) {
        EndDoc(printerDc);
    } else {
        AbortDoc(printerDc);
    }
    DeleteDC(printerDc);

    if (!success) {
        MessageBoxW(hwnd, L"The print job could not be completed.", L"Print", MB_ICONERROR);
        return false;
    }
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
    if (target.RequiresConfirmation() && !ConfirmLocalLinkTarget(hwnd, target)) {
        return;
    }
    switch (target.kind) {
        case LinkTargetKind::MissingLocalPath:
            ShowMissingLocalPath(hwnd, target.path);
            break;
        case LinkTargetKind::InternalDocument:
            if (SamePath(target.path, State(context).currentFilePath)) {
                if (!target.fragment.empty() && !ScrollToFragment(hwnd, context, target.fragment)) {
                    ShowMissingFragment(hwnd, target.fragment);
                }
            } else if (LoadFile(hwnd, context, target.path)) {
                if (!target.fragment.empty() && !ScrollToFragment(hwnd, context, target.fragment)) {
                    ShowMissingFragment(hwnd, target.fragment);
                }
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
    const float width = GetDocumentLayoutWidth(hwnd, context);
    const std::filesystem::path baseDir = appState.currentFilePath.parent_path();

    auto imageSizeProvider = [&context, baseDir](const std::string& url) -> std::pair<float, float> {
        return context.imageCache.GetImageSize(url, baseDir);
    };

    context.controller.QueueRelayout(
            width,
            GetDocumentTypefaces(context),
            imageSizeProvider,
            LayoutOptions{.imageLoadInfoProvider = [&context, baseDir](const std::string& source) {
                return context.imageCache.GetSourceInfo(source, baseDir);
            }});
    SetTimer(hwnd, kRelayoutPollTimerId, 16, nullptr);
    context.lastLayoutWidth = width;
}

void PollDocumentRelayout(HWND hwnd, ViewerHostContext& context) {
    // Input remains live while layout runs. Capture from the displayed layout
    // immediately before replacing it, so a wheel/key scroll is not undone.
    const ScrollAnchor currentAnchor = GetRelayoutScrollAnchor(State(context), GetViewportHeight(hwnd, context));
    RememberRelayoutScrollAnchor(State(context), currentAnchor);
    if (context.controller.PollRelayout()) {
        AppState& appState = State(context);
        RestoreScrollAnchor(appState, currentAnchor, GetViewportHeight(hwnd, context), GetMaxScroll(hwnd, context));
        RememberRelayoutScrollAnchor(appState, currentAnchor);
        RebuildSearchMatches(appState);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (!context.controller.IsRelayoutPending()) {
        KillTimer(hwnd, kRelayoutPollTimerId);
        if (!context.controller.GetLastLayoutError().empty()) {
            context.lastLayoutWidth = -1.0f;
            ShowErrorMessage(hwnd, "Layout failed", context.controller.GetLastLayoutError());
        }
    }
}

bool ReloadCurrentFile(HWND hwnd, ViewerHostContext& context, bool preserveScrollOffset) {
    context.controller.CancelRelayout();
    KillTimer(hwnd, kRelayoutPollTimerId);
    if (!EnsureFontSystem(context)) {
        return false;
    }

    const float width = GetDocumentLayoutWidth(hwnd, context);
    const std::filesystem::path currentPath = State(context).currentFilePath;
    const std::filesystem::path baseDir = currentPath.parent_path();
    const float viewportHeight = GetViewportHeight(hwnd, context);
    const ScrollAnchor scrollAnchor = GetRelayoutScrollAnchor(State(context), viewportHeight);

    context.imageCache.Clear();
    const auto status = context.controller.ReloadCurrentFile(
        width,
        GetDocumentTypefaces(context),
        [&](const DocumentModel& docModel, const std::filesystem::path& preloadBaseDir) {
            context.imageCache.PreloadDocumentImages(docModel, preloadBaseDir);
        },
        [&](const std::string& url) -> std::pair<float, float> {
            return context.imageCache.GetImageSize(url, baseDir);
        },
        LayoutOptions{.imageLoadInfoProvider = [&](const std::string& source) {
            return context.imageCache.GetSourceInfo(source, baseDir);
        }});
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
    context.lastLayoutWidth = width;
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
