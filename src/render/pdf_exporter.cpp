#include "render/pdf_exporter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "app/app_state.h"
#include "render/document_renderer.h"
#include "render/theme.h"
#include "render/typography.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkDocument.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/docs/SkPDFDocument.h"
#pragma warning(pop)

namespace mdviewer {
namespace {

constexpr float kPdfFontScale = 0.88f;
constexpr float kMinPageAdvance = 72.0f;
constexpr float kPageBreakBottomSlack = 8.0f;
constexpr float kPdfPageMarginTop = 42.0f;
constexpr float kPdfPageMarginBottom = 48.0f;

struct PdfPageRange {
    float top = 0.0f;
    float bottom = 0.0f;
};

class FilesystemWStream final : public SkWStream {
public:
    explicit FilesystemWStream(const std::filesystem::path& path)
        : output_(path, std::ios::binary), failed_(!output_.is_open()) {}

    bool write(const void* buffer, size_t size) override {
        if (!output_.is_open() || failed_) {
            failed_ = true;
            return false;
        }
        if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            failed_ = true;
            return false;
        }

        output_.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
        if (!output_) {
            failed_ = true;
            return false;
        }

        bytesWritten_ += size;
        return true;
    }

    void flush() override {
        if (!output_.is_open()) {
            failed_ = true;
            return;
        }
        output_.flush();
        if (!output_) {
            failed_ = true;
        }
    }

    size_t bytesWritten() const override {
        return bytesWritten_;
    }

    bool isOpen() const {
        return output_.is_open();
    }

    bool isValid() const {
        return output_.is_open() && !failed_;
    }

private:
    std::ofstream output_;
    size_t bytesWritten_ = 0;
    bool failed_ = false;
};

std::string PathFilenameUtf8(const std::filesystem::path& path) {
    const auto filename = path.filename().u8string();
    return std::string(filename.begin(), filename.end());
}

bool EnsureParentDirectory(const std::filesystem::path& outputPath) {
    const std::filesystem::path parentPath = outputPath.parent_path();
    if (parentPath.empty()) {
        return true;
    }

    std::error_code createError;
    std::filesystem::create_directories(parentPath, createError);
    return !createError;
}

SkPDF::Metadata MakePdfMetadata(const std::filesystem::path& sourcePath) {
    SkPDF::Metadata metadata;
    const std::string title = PathFilenameUtf8(sourcePath);
    if (!title.empty()) {
        metadata.fTitle = SkString(title.c_str());
    }
    metadata.fCreator = SkString("Simple Markdown Viewer");
    metadata.fProducer = SkString("Simple Markdown Viewer");
    metadata.allowNoJpegs = true;
    return metadata;
}

void PopulateExportState(
    AppState& state,
    const PdfExportRequest& request,
    DocumentLayout layout,
    float exportBaseFontSize) {
    state.currentFilePath = request.sourcePath;
    state.sourceText = request.sourceText;
    state.docModel = request.document;
    state.docLayout = std::move(layout);
    state.theme = request.theme;
    state.baseFontSize = exportBaseFontSize;
    state.outlineCollapsed = true;
    state.selectionAnchor = 0;
    state.selectionFocus = 0;
    state.hoveredUrl.clear();
    state.searchActive = false;
    state.searchQuery.clear();
    state.searchMatches.clear();
    state.copiedFeedbackTimeout = 0;
    state.zoomFeedbackTimeout = 0;
    state.isAutoScrolling = false;
    state.isDraggingScrollbar = false;
    state.isSelecting = false;
}

void AppendPageBreakCandidates(const std::vector<BlockLayout>& blocks, std::vector<float>& candidates) {
    for (const BlockLayout& block : blocks) {
        if (block.bounds.top() > 0.0f) {
            candidates.push_back(block.bounds.top());
        }
        for (const LineLayout& line : block.lines) {
            if (line.y > 0.0f) {
                candidates.push_back(line.y);
            }
        }
        if (!block.children.empty()) {
            AppendPageBreakCandidates(block.children, candidates);
        }
    }
}

float FindFirstContentTop(const std::vector<BlockLayout>& blocks) {
    float firstTop = std::numeric_limits<float>::max();
    for (const BlockLayout& block : blocks) {
        firstTop = std::min(firstTop, block.bounds.top());
        if (!block.children.empty()) {
            firstTop = std::min(firstTop, FindFirstContentTop(block.children));
        }
    }

    return firstTop == std::numeric_limits<float>::max() ? 0.0f : std::max(firstTop, 0.0f);
}

std::vector<PdfPageRange> ComputePageRanges(const DocumentLayout& layout, float contentHeight) {
    std::vector<PdfPageRange> pages;
    const float totalHeight = std::max(layout.totalHeight, 1.0f);
    if (totalHeight <= contentHeight) {
        pages.push_back({0.0f, totalHeight});
        return pages;
    }

    std::vector<float> candidates;
    candidates.reserve(layout.blocks.size() * 2);
    AppendPageBreakCandidates(layout.blocks, candidates);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    float pageTop = 0.0f;
    while (pageTop < totalHeight) {
        if (pageTop + contentHeight >= totalHeight) {
            pages.push_back({pageTop, totalHeight});
            break;
        }

        const float desiredBreak = pageTop + contentHeight - kPageBreakBottomSlack;
        float pageBottom = pageTop + contentHeight;
        const auto upper = std::upper_bound(candidates.begin(), candidates.end(), desiredBreak);
        if (upper != candidates.begin()) {
            const float candidate = *std::prev(upper);
            if (candidate > pageTop + kMinPageAdvance) {
                pageBottom = candidate;
            }
        }

        if (pageBottom <= pageTop + 1.0f) {
            pageBottom = pageTop + contentHeight;
        }

        pages.push_back({pageTop, pageBottom});
        pageTop = pageBottom;
    }

    return pages;
}

} // namespace

PdfExportStatus ExportMarkdownToPdf(const PdfExportRequest& request) {
    if (request.outputPath.empty() || request.sourceText.empty() || request.document.blocks.empty()) {
        return PdfExportStatus::NoDocument;
    }
    if (!request.layoutTypeface || !request.typefaces.regular) {
        return PdfExportStatus::NoDocument;
    }
    if (request.pageWidth <= 1.0f || request.pageHeight <= 1.0f) {
        return PdfExportStatus::WriteError;
    }
    const float contentHeight = request.pageHeight - kPdfPageMarginTop - kPdfPageMarginBottom;
    if (contentHeight <= kMinPageAdvance) {
        return PdfExportStatus::WriteError;
    }

    const float exportBaseFontSize = ClampBaseFontSize(request.baseFontSize * kPdfFontScale);
    DocumentLayout layout = LayoutEngine::ComputeLayout(
        request.document,
        request.pageWidth,
        request.layoutTypeface,
        exportBaseFontSize,
        request.imageSizeProvider);
    AppState exportState;
    PopulateExportState(exportState, request, std::move(layout), exportBaseFontSize);

    SkDynamicMemoryWStream pdfBytes;
    sk_sp<SkDocument> pdfDocument = SkPDF::MakeDocument(&pdfBytes, MakePdfMetadata(request.sourcePath));
    if (!pdfDocument) {
        return PdfExportStatus::PdfBackendUnavailable;
    }

    const ThemePalette palette = GetThemePalette(request.theme);
    const std::vector<PdfPageRange> pages = ComputePageRanges(exportState.docLayout, contentHeight);

    const float firstContentTop = FindFirstContentTop(exportState.docLayout.blocks);
    for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const PdfPageRange& page = pages[pageIndex];
        const float renderTop = pageIndex == 0 ? std::min(firstContentTop, page.bottom - 1.0f) : page.top;
        const float pageContentHeight = std::clamp(page.bottom - renderTop, 1.0f, contentHeight);
        SkCanvas* canvas = pdfDocument->beginPage(request.pageWidth, request.pageHeight);
        if (!canvas) {
            pdfDocument->abort();
            return PdfExportStatus::WriteError;
        }

        canvas->clear(palette.windowBackground);
        exportState.scrollOffset = renderTop;
        canvas->save();
        canvas->clipRect(SkRect::MakeXYWH(0.0f, kPdfPageMarginTop, request.pageWidth, pageContentHeight));
        RenderDocumentScene(
            DocumentSceneParams{
                .canvas = canvas,
                .appState = &exportState,
                .palette = palette,
                .typefaces = request.typefaces,
                .baseFontSize = exportState.baseFontSize,
                .contentTopInset = kPdfPageMarginTop,
                .viewportHeight = pageContentHeight,
                .surfaceWidth = request.pageWidth,
                .surfaceHeight = request.pageHeight,
                .documentLeftInset = 0.0f,
                .scrollbarWidth = 0.0f,
                .scrollbarMargin = 0.0f,
                .currentTickCount = 0,
                .visibleDocumentTop = renderTop,
                .visibleDocumentBottom = page.bottom,
                .showInteractiveElements = false,
                .scrollbarThumbRect = std::nullopt,
                .resolveImage = request.resolveImage,
                .addCodeBlockButton = nullptr,
            });
        canvas->restore();

        pdfDocument->endPage();
    }

    pdfDocument->close();
    if (pdfBytes.bytesWritten() == 0) {
        return PdfExportStatus::WriteError;
    }
    if (!EnsureParentDirectory(request.outputPath)) {
        return PdfExportStatus::FileOpenError;
    }

    FilesystemWStream output(request.outputPath);
    if (!output.isOpen()) {
        return PdfExportStatus::FileOpenError;
    }
    if (!pdfBytes.writeToStream(&output)) {
        return PdfExportStatus::WriteError;
    }
    output.flush();
    return output.isValid() ? PdfExportStatus::Success : PdfExportStatus::WriteError;
}

const char* PdfExportStatusMessage(PdfExportStatus status) {
    switch (status) {
        case PdfExportStatus::Success:
            return "PDF saved successfully.";
        case PdfExportStatus::NoDocument:
            return "Open a Markdown file before saving as PDF.";
        case PdfExportStatus::FileOpenError:
            return "Could not open the PDF file for writing.";
        case PdfExportStatus::WriteError:
            return "Could not write the PDF file.";
        case PdfExportStatus::PdfBackendUnavailable:
            return "PDF export is unavailable because Skia was built without PDF support.";
        default:
            return "Could not save the PDF file.";
    }
}

} // namespace mdviewer
