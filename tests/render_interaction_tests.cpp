#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/app_state.h"
#include "markdown/markdown_parser.h"
#include "render/document_renderer.h"
#include "render/native_badge_renderer.h"
#include "text/text_geometry.h"
#include "util/skia_font_utils.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkPixmap.h"

namespace mdviewer::tests {
namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

DocumentTypefaceSet Fonts(SkFontMgr* manager, SkTypeface* face) {
    return {.fontMgr = manager, .regular = face, .bold = face, .heading = face, .code = face};
}

void TableHitTestingUsesBothCoordinates() {
    DocumentLayout layout;
    BlockLayout table;
    table.type = BlockType::Table;
    table.bounds = SkRect::MakeXYWH(10, 10, 300, 50);
    table.tableTsv = "first\tsecond\tthird";
    table.tableCsv = "first,second,third";
    BlockLayout row;
    row.type = BlockType::TableRow;
    row.bounds = table.bounds;
    for (size_t column = 0; column < 3; ++column) {
        BlockLayout cell;
        cell.type = BlockType::TableCell;
        cell.bounds = SkRect::MakeXYWH(10.0f + static_cast<float>(column) * 100.0f, 10, 100, 50);
        cell.usesHorizontalScrollOffset = true;
        LineLayout line;
        line.x = cell.bounds.left() + 10;
        line.y = 20;
        line.height = 20;
        line.width = 60;
        line.textStart = column * 5;
        line.textLength = 4;
        RunLayout run;
        run.text = "link";
        run.textStart = line.textStart;
        run.visualWidth = 60;
        run.linkTarget = "https://example.com/" + std::to_string(column);
        run.bidiLevel = column == 1 ? 1 : 0;
        run.caretStops = column == 1
            ? std::vector<CaretStop>{{line.textStart + 4, 0}, {line.textStart, 60}}
            : std::vector<CaretStop>{{line.textStart, 0}, {line.textStart + 4, 60}};
        line.runs.push_back(std::move(run));
        cell.lines.push_back(std::move(line));
        row.children.push_back(std::move(cell));
    }
    table.children.push_back(std::move(row));
    layout.blocks.push_back(std::move(table));
    for (size_t column = 0; column < 3; ++column) {
        const auto hit = HitTestDocument(layout, 0, 0, 30.0f + static_cast<float>(column) * 100.0f, 30,
            {.includeTableData = false});
        Require(hit.linkTarget == "https://example.com/" + std::to_string(column),
            "every table column must hit its own link, including RTL text");
        Require(hit.tableCsv.empty() && hit.tableTsv.empty(),
            "hover hit testing must not copy table clipboard payloads");
    }
    const HitTestCallbacks scrolled{
        .get_block_horizontal_scroll = [](const BlockLayout& block) {
            return block.usesHorizontalScrollOffset ? 80.0f : 0.0f;
        },
        .includeTableData = false,
    };
    const auto rtlHit = HitTestDocument(layout, 0, 0, 99.0f, 30, scrolled);
    Require(rtlHit.linkTarget == "https://example.com/1" && rtlHit.position == 5,
        "scrolled RTL cells must retain the correct logical caret and link");
    const auto context = HitTestDocument(layout, 0, 0, 30, 30, {.includeTableData = true});
    Require(context.tableTsv == "first\tsecond\tthird" && context.tableCsv == "first,second,third",
        "context menu hits must still provide clipboard representations on demand");
}

void SearchCacheTracksLogicalContent() {
    AppState state;
    state.docLayout.plainText = "ALPHA beta alpha";
    OpenSearch(state);
    InsertSearchText(state, "a");
    Require(state.searchTextCache.buildCount == 1 && state.searchTextCache.originalOffsets.empty(),
        "ASCII search should build one index without a byte-offset array");
    InsertSearchText(state, "lpha");
    Require(state.searchTextCache.buildCount == 1 && state.searchMatches.size() == 2,
        "typing must reuse the folded document index");
    DocumentLayout sameText;
    sameText.plainText = state.docLayout.plainText;
    state.ReplaceDocumentLayout(std::move(sameText));
    RebuildSearchMatches(state);
    Require(state.searchTextCache.buildCount == 1,
        "width-only layout changes must preserve the search index");
    DocumentLayout changedText;
    changedText.plainText = "Stra\xC3\x9F" "e";
    state.ReplaceDocumentLayout(std::move(changedText));
    state.searchQuery = "STRASSE";
    RebuildSearchMatches(state);
    Require(state.searchTextCache.buildCount == 2 && state.searchMatches.size() == 1 &&
        state.searchMatches.front().second == 7,
        "new logical content must rebuild Unicode mappings");
    state.docLayout.plainText = "\xC3\x9F";
    state.searchQuery = "s";
    RebuildSearchMatches(state);
    Require(state.searchMatches.empty(), "search must not match half a case-fold expansion");
    state.searchQuery = "ss";
    RebuildSearchMatches(state);
    Require(state.searchMatches.size() == 1 && state.searchMatches.front().second == 2,
        "complete case-fold expansions must map back to the original UTF-8 bytes");
}

void IndexedCaretGeometryPreservesDirections() {
    RunLayout run;
    run.text = "abc";
    run.textStart = 10;
    run.visualWidth = 30;
    run.caretStops = {{10, 0}, {10, 2}, {11, 10}, {13, 30}};
    run.caretStopsLogicalOrder = 1;
    auto rect = GetTextRunRangeRect(run, 100, 10, 11, 0, 20);
    Require(rect && rect->left() == 100 && rect->right() == 110,
        "indexed LTR geometry must retain the first visual stop at duplicate boundaries");
    run.caretStops = {{13, 0}, {11, 10}, {10, 20}, {10, 30}};
    run.caretStopsLogicalOrder = -1;
    rect = GetTextRunRangeRect(run, 100, 10, 11, 0, 20);
    Require(rect && rect->left() == 110 && rect->right() == 120,
        "indexed RTL geometry must preserve logical boundaries and the visual-first tie policy");
    Require(HitTestTextRun(run, 4) == 13 && HitTestTextRun(run, 26) == 10,
        "binary visual caret lookup must preserve RTL selection direction");
}

void RendererVisitsOnlyVisibleLines(const DocumentTypefaceSet& fonts) {
    AppState state;
    state.sourceText = "image fixture";
    state.outlineCollapsed = true;
    state.scrollOffset = 100000;
    BlockLayout block;
    block.type = BlockType::Paragraph;
    block.bounds = SkRect::MakeXYWH(0, 0, 200, 200000);
    for (size_t index = 0; index < 10000; ++index) {
        LineLayout line;
        line.x = 10;
        line.y = static_cast<float>(index) * 20;
        line.height = 20;
        RunLayout image;
        image.kind = InlineKind::Image;
        image.imageSource = "local.png";
        image.imageWidth = 16;
        image.imageHeight = 16;
        image.visualWidth = 16;
        line.runs.push_back(std::move(image));
        block.lines.push_back(std::move(line));
    }
    state.docLayout.blocks.push_back(std::move(block));
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(300, 100));
    Require(surface != nullptr, "renderer regression surface must initialize");
    size_t resolutions = 0;
    RenderDocumentScene({
        .canvas = surface->getCanvas(), .appState = &state,
        .palette = GetThemePalette(ThemeMode::Light), .typefaces = fonts,
        .viewportHeight = 100, .surfaceWidth = 300, .surfaceHeight = 100,
        .visibleDocumentTop = 100000, .visibleDocumentBottom = 100100,
        .showInteractiveElements = false,
        .resolveImage = [&](const std::string&, float, float) -> sk_sp<SkImage> {
            ++resolutions;
            return nullptr;
        },
    });
    Require(resolutions >= 5 && resolutions <= 8,
        "one long block must resolve/draw only viewport lines, not all 10,000 lines");
}

void RemotePlaceholderActions(const DocumentTypefaceSet& fonts) {
    const std::string source = "https://example.com/" + std::string(240, 'x') + ".png?token=full-source";
    const ImageLoadInfo info{ImageSourceKind::RemoteUrl, ImageLoadState::Blocked, source, source, {}};
    LayoutOptions options;
    options.imageLoadInfoProvider = [&](const std::string&) { return info; };
    AppState state;
    state.sourceText = "![image](" + source + ")";
    state.outlineCollapsed = true;
    state.docLayout = LayoutEngine::ComputeLayout(MarkdownParser::Parse(state.sourceText), 320, fonts, 17, {}, options);
    Require(!state.docLayout.blocks.empty() && !state.docLayout.blocks.front().lines.empty(),
        "remote image placeholder must produce layout");
    const RunLayout& run = state.docLayout.blocks.front().lines.front().runs.front();
    std::string displayedSource;
    for (const auto& fragment : run.imageSourceLines) displayedSource += fragment;
    Require(run.imageLoadRequired && displayedSource == source && run.imageSourceLines.size() > 1,
        "remote placeholders must wrap the complete exact URL without truncation");
    const auto& line = state.docLayout.blocks.front().lines.front();
    const auto bodyHit = HitTestDocument(state.docLayout, 0, 0,
        line.x + run.visualX + run.imageWidth * 0.5f, line.y + line.height * 0.5f, {});
    Require(bodyHit.url.empty() && bodyHit.linkTarget.empty() && bodyHit.imageSource == source,
        "placeholder body clicks must not activate network targets without the Load image action");
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 800));
    Require(surface != nullptr, "remote placeholder test surface must initialize");
    size_t imageResolutions = 0;
    std::vector<ImageLoadButtonRegion> buttons;
    ImageLoadState currentState = ImageLoadState::Blocked;
    DocumentSceneParams params{
        .canvas = surface->getCanvas(), .appState = &state,
        .palette = GetThemePalette(ThemeMode::Light), .typefaces = fonts,
        .viewportHeight = 800, .surfaceWidth = 320, .surfaceHeight = 800,
        .visibleDocumentBottom = 800,
        .resolveImage = [&](const std::string&, float, float) -> sk_sp<SkImage> {
            ++imageResolutions;
            return nullptr;
        },
        .imageLoadInfoProvider = [&](const std::string&) {
            auto result = info;
            result.state = currentState;
            return result;
        },
        .addImageLoadButton = [&](const ImageLoadButtonRegion& button) { buttons.push_back(button); },
    };
    RenderDocumentScene(params);
    Require(imageResolutions == 0 && buttons.size() == 1 && buttons.front().source == source,
        "blocked images must only register explicit load actions, without resolving the image");
    currentState = ImageLoadState::Loading;
    buttons.clear();
    RenderDocumentScene(params);
    Require(buttons.empty(), "loading images must disable duplicate requests");
    currentState = ImageLoadState::Failed;
    RenderDocumentScene(params);
    Require(buttons.size() == 1, "failed image loads must offer a retry action");
    ImageLoadInfo failure = info;
    failure.state = ImageLoadState::Failed;
    failure.error = "Redirect blocked: https://another.example/" + std::string(200, 'r');
    options.imageLoadInfoProvider = [&](const std::string&) { return failure; };
    const auto failedLayout = LayoutEngine::ComputeLayout(
        MarkdownParser::Parse(state.sourceText), 320, fonts, 17, {}, options);
    const auto& failedRun = failedLayout.blocks.front().lines.front().runs.front();
    std::string displayedError;
    for (const auto& fragment : failedRun.imageErrorLines) displayedError += fragment;
    Require(displayedError == failure.error && failedRun.imageHeight > run.imageHeight,
        "failure placeholders must reserve room for the complete diagnostic and redirect destination");
    failure.kind = ImageSourceKind::Local;
    failure.originalSource = "linked-image.png";
    failure.displayTarget = failure.originalSource;
    failure.error = "Image path contains an unsupported symbolic link.";
    state.docLayout = LayoutEngine::ComputeLayout(
        MarkdownParser::Parse("![image](linked-image.png)"), 320, fonts, 17, {}, options);
    buttons.clear();
    params.imageLoadInfoProvider = [&](const std::string&) { return failure; };
    RenderDocumentScene(params);
    Require(buttons.empty() && imageResolutions == 0,
        "local rejected image paths must show their error without a network consent or retry button");
}

} // namespace

void RunRenderInteractionTests() {
    TableHitTestingUsesBothCoordinates();
    SearchCacheTracksLogicalContent();
    IndexedCaretGeometryPreservesDirections();
    auto manager = CreateFontManager();
    auto face = CreateDefaultTypeface(manager);
    Require(manager && face, "render interaction tests require a document font");
    for (const auto* url : {"https://img.shields.io/badge/license-MIT-blue",
                           "https://img.shields.io/badge/status-local--only-green"}) {
        for (float scale : {1.0f, 1.25f, 2.0f}) {
            for (float zoom : {0.8f, 1.0f, 1.3f}) {
                auto first = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(500, 100));
                auto second = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(500, 100));
                const auto badge = ParseNativeBadge(url);
                Require(badge.has_value(), "badge fixture parses");
                auto draw = [&](SkSurface* surface, float offset) {
                    auto* canvas = surface->getCanvas();
                    canvas->clear(SK_ColorTRANSPARENT);
                    canvas->scale(scale, scale);
                    DrawNativeBadge(canvas, *badge, face,
                        SkRect::MakeXYWH(offset / scale, offset / scale, 100 * zoom, 20 * zoom), true);
                };
                draw(first.get(), 10.1f);
                draw(second.get(), 10.4f);
                SkPixmap a, b;
                Require(first->peekPixels(&a) && second->peekPixels(&b), "badge pixels available");
                Require(a.computeByteSize() == b.computeByteSize() &&
                    std::memcmp(a.addr(), b.addr(), a.computeByteSize()) == 0,
                    "fractional badge positions must not change sharpness at any zoom/display scale");
            }
        }
    }
    const auto fonts = Fonts(manager.get(), face.get());
    RendererVisitsOnlyVisibleLines(fonts);
    RemotePlaceholderActions(fonts);
}

} // namespace mdviewer::tests
