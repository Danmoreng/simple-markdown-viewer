#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "layout/document_model.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "include/core/SkRect.h"

class SkTypeface;

namespace mdviewer {

struct RunLayout {
    InlineFormatting formatting = InlineFormatting::None;
    InlineKind kind = InlineKind::Text;
    SyntaxRole syntaxRole = SyntaxRole::None;
    std::string text;
    std::string imageSource;
    size_t textStart = 0;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
    std::string linkTarget;
};

struct LineLayout {
    float x = 0.0f;
    float y;
    float height;
    size_t textStart = 0;
    size_t textLength = 0;
    std::vector<RunLayout> runs;
};

struct BlockLayout {
    BlockType type;
    TextAlign align = TextAlign::Default;
    TaskListState taskListState = TaskListState::None;
    AlertKind alertKind = AlertKind::None;
    unsigned orderedListStart = 1;
    char orderedListDelimiter = '.';
    std::string codeLanguage;
    syntax::HighlightStatus codeHighlightStatus = syntax::HighlightStatus::NotRequested;
    float codeContentWidth = 0.0f;
    float codeViewportWidth = 0.0f;
    float horizontalContentWidth = 0.0f;
    float horizontalViewportWidth = 0.0f;
    bool usesHorizontalScrollOffset = false;
    size_t horizontalScrollOwnerTextStart = 0;
    SkRect bounds;
    size_t textStart = 0;
    size_t textLength = 0;
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

class LayoutEngine {
public:
    using ImageSizeProvider = std::function<std::pair<float, float>(const std::string& url)>;
    static DocumentLayout ComputeLayout(
        const DocumentModel& doc,
        float width,
        SkTypeface* typeface,
        float baseFontSize,
        ImageSizeProvider imageSizeProvider = nullptr);
};

} // namespace mdviewer
