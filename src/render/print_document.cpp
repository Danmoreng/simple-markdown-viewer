#include "render/print_document.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "render/document_renderer.h"
#include "render/theme.h"
#include "render/typography.h"

namespace mdviewer {
namespace {

constexpr float kPageBreakBottomSlack = 8.0f;

void PopulatePrintState(
    AppState& state,
    const PrintDocumentRequest& request,
    DocumentLayout layout,
    float printBaseFontSize) {
    state.currentFilePath = request.sourcePath;
    state.sourceText = request.sourceText;
    state.docModel = request.document;
    state.docLayout = std::move(layout);
    state.theme = request.theme;
    state.baseFontSize = printBaseFontSize;
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

std::vector<PrintPageRange> ComputePageRanges(const DocumentLayout& layout, float contentHeight) {
    std::vector<PrintPageRange> pages;
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
            if (candidate > pageTop + kMinPrintPageAdvance) {
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

bool PreparePrintDocument(const PrintDocumentRequest& request, PreparedPrintDocument& prepared) {
    if (request.sourceText.empty() || request.document.blocks.empty()) {
        return false;
    }
    if (!request.layoutTypeface || !request.typefaces.regular) {
        return false;
    }
    if (request.pageWidth <= 1.0f || request.pageHeight <= 1.0f) {
        return false;
    }

    const float contentHeight = request.pageHeight - request.marginTop - request.marginBottom;
    if (contentHeight <= kMinPrintPageAdvance) {
        return false;
    }

    const float printBaseFontSize = ClampBaseFontSize(request.baseFontSize * request.fontScale);
    DocumentLayout layout = LayoutEngine::ComputeLayout(
        request.document,
        request.pageWidth,
        request.layoutTypeface,
        printBaseFontSize,
        request.imageSizeProvider);

    prepared.typefaces = request.typefaces;
    prepared.pages.clear();
    prepared.pageWidth = request.pageWidth;
    prepared.pageHeight = request.pageHeight;
    prepared.marginTop = request.marginTop;
    prepared.contentHeight = contentHeight;
    PopulatePrintState(prepared.appState, request, std::move(layout), printBaseFontSize);
    prepared.pages = ComputePageRanges(prepared.appState.docLayout, contentHeight);
    return !prepared.pages.empty();
}

void RenderPrintDocumentPage(
    PreparedPrintDocument& prepared,
    size_t pageIndex,
    SkCanvas* canvas,
    std::function<sk_sp<SkImage>(const std::string& url, float displayWidth, float displayHeight)> resolveImage) {
    if (!canvas || pageIndex >= prepared.pages.size()) {
        return;
    }

    const PrintPageRange& page = prepared.pages[pageIndex];
    const float firstContentTop = pageIndex == 0 ? FindFirstContentTop(prepared.appState.docLayout.blocks) : page.top;
    const float renderTop = pageIndex == 0 ? std::min(firstContentTop, page.bottom - 1.0f) : page.top;
    const float pageContentHeight = std::clamp(page.bottom - renderTop, 1.0f, prepared.contentHeight);
    const ThemePalette palette = GetThemePalette(prepared.appState.theme);

    canvas->clear(palette.windowBackground);
    prepared.appState.scrollOffset = renderTop;
    canvas->save();
    canvas->clipRect(SkRect::MakeXYWH(0.0f, prepared.marginTop, prepared.pageWidth, pageContentHeight));
    RenderDocumentScene(
        DocumentSceneParams{
            .canvas = canvas,
            .appState = &prepared.appState,
            .palette = palette,
            .typefaces = prepared.typefaces,
            .baseFontSize = prepared.appState.baseFontSize,
            .contentTopInset = prepared.marginTop,
            .viewportHeight = pageContentHeight,
            .surfaceWidth = prepared.pageWidth,
            .surfaceHeight = prepared.pageHeight,
            .documentLeftInset = 0.0f,
            .scrollbarWidth = 0.0f,
            .scrollbarMargin = 0.0f,
            .currentTickCount = 0,
            .visibleDocumentTop = renderTop,
            .visibleDocumentBottom = page.bottom,
            .showInteractiveElements = false,
            .scrollbarThumbRect = std::nullopt,
            .resolveImage = std::move(resolveImage),
            .addCodeBlockButton = nullptr,
        });
    canvas->restore();
}

} // namespace mdviewer
