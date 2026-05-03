#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include "layout/document_model.h"
#include "include/core/SkRect.h"

class SkTypeface;

namespace mdviewer {

struct RunLayout {
    InlineStyle style;
    std::string text;
    std::string url;
    size_t textStart = 0;
    float imageWidth = 0.0f;
    float imageHeight = 0.0f;
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
    unsigned orderedListStart = 1;
    char orderedListDelimiter = '.';
    std::string codeLanguage;
    SkRect bounds;
    size_t textStart = 0;
    size_t textLength = 0;
    std::vector<LineLayout> lines;
    std::vector<BlockLayout> children;
};

struct DocumentLayout {
    std::vector<BlockLayout> blocks;
    float totalHeight;
    std::string plainText;
    std::unordered_map<std::string, float> anchors;
};

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
