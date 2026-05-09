#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/app_state.h"
#include "layout/document_model.h"
#include "layout/layout_engine.h"
#include "render/document_typefaces.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace mdviewer {

inline constexpr float kPrintFontScale = 0.88f;
inline constexpr float kPrintPageMarginTop = 42.0f;
inline constexpr float kPrintPageMarginBottom = 48.0f;
inline constexpr float kMinPrintPageAdvance = 72.0f;

struct PrintPageRange {
    float top = 0.0f;
    float bottom = 0.0f;
};

struct PrintDocumentRequest {
    std::filesystem::path sourcePath;
    std::string sourceText;
    DocumentModel document;
    ThemeMode theme = ThemeMode::Light;
    float baseFontSize = 17.0f;
    DocumentTypefaceSet typefaces = {};
    SkTypeface* layoutTypeface = nullptr;
    LayoutEngine::ImageSizeProvider imageSizeProvider;
    float pageWidth = 595.0f;
    float pageHeight = 842.0f;
    float marginTop = kPrintPageMarginTop;
    float marginBottom = kPrintPageMarginBottom;
    float fontScale = kPrintFontScale;
};

struct PreparedPrintDocument {
    AppState appState;
    DocumentTypefaceSet typefaces = {};
    std::vector<PrintPageRange> pages;
    float pageWidth = 595.0f;
    float pageHeight = 842.0f;
    float marginTop = kPrintPageMarginTop;
    float contentHeight = 0.0f;
};

bool PreparePrintDocument(const PrintDocumentRequest& request, PreparedPrintDocument& prepared);
void RenderPrintDocumentPage(
    PreparedPrintDocument& prepared,
    size_t pageIndex,
    SkCanvas* canvas,
    std::function<sk_sp<SkImage>(const std::string& url, float displayWidth, float displayHeight)> resolveImage);

} // namespace mdviewer
