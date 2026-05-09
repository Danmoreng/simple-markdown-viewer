#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "app/app_config.h"
#include "layout/document_model.h"
#include "layout/layout_engine.h"
#include "render/document_typefaces.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace mdviewer {

inline constexpr float kDefaultPdfPageWidthPt = 595.0f;
inline constexpr float kDefaultPdfPageHeightPt = 842.0f;

enum class PdfExportStatus {
    Success,
    NoDocument,
    FileOpenError,
    WriteError,
    PdfBackendUnavailable
};

struct PdfExportRequest {
    std::filesystem::path outputPath;
    std::filesystem::path sourcePath;
    std::string sourceText;
    DocumentModel document;
    ThemeMode theme = ThemeMode::Light;
    float baseFontSize = 17.0f;
    DocumentTypefaceSet typefaces = {};
    SkTypeface* layoutTypeface = nullptr;
    LayoutEngine::ImageSizeProvider imageSizeProvider;
    std::function<sk_sp<SkImage>(const std::string& url, float displayWidth, float displayHeight)> resolveImage;
    float pageWidth = kDefaultPdfPageWidthPt;
    float pageHeight = kDefaultPdfPageHeightPt;
};

PdfExportStatus ExportMarkdownToPdf(const PdfExportRequest& request);
const char* PdfExportStatusMessage(PdfExportStatus status);

} // namespace mdviewer
