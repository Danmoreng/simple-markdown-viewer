#pragma once
#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "layout/document_model.h"
#include "render/math_renderer.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "text/document_fonts.h"
#include "text/text_direction.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkFont.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypes.h"
#pragma warning(pop)

namespace mdviewer {

struct CaretStop {
    size_t textPosition = 0;
    float x = 0.0f;
};

struct RunLayout {
    InlineFormatting formatting = InlineFormatting::None;
    InlineKind kind = InlineKind::Text;
    MetadataRunRole metadataRole = MetadataRunRole::None;
    SyntaxRole syntaxRole = SyntaxRole::None;
    std::string text;
    std::string imageSource;
    std::string mathSource;
    size_t textStart = 0;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
    float visualX = 0.0f;
    float visualWidth = 0.0f;
    uint8_t bidiLevel = 0;
    size_t semanticSpanId = 0;
    bool shaped = false;
    float baselineShift = 0.0f;
    bool mathDisplay = false;
    MathLayout mathLayout;
    std::string linkTarget;
    SkFont shapedFont;
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> glyphPositions;
    std::vector<uint32_t> glyphClusters;
    std::vector<CaretStop> caretStops;
};

struct LineLayout {
    float x = 0.0f;
    float y;
    float height;
    float width = 0.0f;
    ResolvedTextDirection direction = ResolvedTextDirection::LeftToRight;
    size_t textStart = 0;
    size_t textLength = 0;
    std::vector<RunLayout> runs;
};

struct BlockLayout {
    BlockType type;
    TextAlign align = TextAlign::Default;
    ResolvedTextDirection direction = ResolvedTextDirection::LeftToRight;
    TaskListState taskListState = TaskListState::None;
    AlertKind alertKind = AlertKind::None;
    unsigned orderedListStart = 1;
    char orderedListDelimiter = '.';
    std::string codeLanguage;
    std::string metadataFormat;
    size_t detailsId = 0;
    bool detailsOpen = false;
    syntax::HighlightStatus codeHighlightStatus = syntax::HighlightStatus::NotRequested;
    float codeContentWidth = 0.0f;
    float codeViewportWidth = 0.0f;
    float horizontalContentWidth = 0.0f;
    float horizontalViewportWidth = 0.0f;
    bool usesHorizontalScrollOffset = false;
    size_t horizontalScrollOwnerTextStart = 0;
    float fontScale = 1.0f;
    SkRect bounds;
    size_t textStart = 0;
    size_t textLength = 0;
    std::string tableTsv;
    std::string tableCsv;
    std::vector<LineLayout> lines;
    std::vector<BlockLayout> children;
};

struct HeadingOutlineItem {
    int level = 1;
    std::string text;
    std::string slug;
    float y = 0.0f;
};

struct DocumentLayout {
    std::vector<BlockLayout> blocks;
    float totalHeight;
    std::string plainText;
    std::unordered_map<std::string, float> anchors;
    std::vector<HeadingOutlineItem> outline;
};

struct DocumentHorizontalInsets {
    float left = 0.0f;
    float right = 0.0f;
};

DocumentHorizontalInsets GetDocumentHorizontalInsets(float viewportWidth);

struct LayoutOptions {
    bool fitHorizontalOverflow = false;
    bool reserveHorizontalScrollbarSpace = true;
    float minimumHorizontalFitScale = 0.72f;
};

class LayoutEngine {
public:
    using ImageSizeProvider = std::function<std::pair<float, float>(const std::string& url)>;
    static DocumentLayout ComputeLayout(
        const DocumentModel& doc,
        float width,
        const DocumentTypefaceSet& typefaces,
        float baseFontSize,
        ImageSizeProvider imageSizeProvider = nullptr,
        LayoutOptions options = {});
};

} // namespace mdviewer
