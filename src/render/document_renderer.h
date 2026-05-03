#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "app/app_state.h"
#include "render/document_typefaces.h"
#include "render/theme.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace mdviewer {

struct DocumentSceneParams {
    SkCanvas* canvas = nullptr;
    const AppState* appState = nullptr;
    ThemePalette palette = {};
    DocumentTypefaceSet typefaces = {};
    float baseFontSize = 17.0f;
    float contentTopInset = 0.0f;
    float viewportHeight = 0.0f;
    float surfaceWidth = 0.0f;
    float surfaceHeight = 0.0f;
    float documentLeftInset = 0.0f;
    float scrollbarWidth = 0.0f;
    float scrollbarMargin = 0.0f;
    uint64_t currentTickCount = 0;
    float visibleDocumentTop = 0.0f;
    float visibleDocumentBottom = 0.0f;
    std::optional<SkRect> scrollbarThumbRect;
    std::function<sk_sp<SkImage>(const std::string& url, float displayWidth, float displayHeight)> resolveImage;
    std::function<void(const SkRect& rect, size_t start, size_t end)> addCodeBlockButton;
};

bool IsHeadingBlock(BlockType blockType);
void ConfigureDocumentFont(
    SkFont& font,
    const DocumentTypefaceSet& typefaces,
    BlockType blockType,
    InlineStyle inlineStyle,
    float baseFontSize);
SkColor GetDocumentTextColor(const ThemePalette& palette, BlockType blockType, InlineStyle inlineStyle);
float GetDocumentContentX(const BlockLayout& block);

void RenderDocumentScene(const DocumentSceneParams& params);

} // namespace mdviewer
