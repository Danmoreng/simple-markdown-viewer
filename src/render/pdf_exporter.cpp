#include "render/pdf_exporter.h"

#include <cstddef>
#include <fstream>
#include <limits>
#include <system_error>

#include "render/print_document.h"
#include "render/theme.h"

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

    PreparedPrintDocument prepared;
    if (!PreparePrintDocument(
            PrintDocumentRequest{
                .sourcePath = request.sourcePath,
                .sourceText = request.sourceText,
                .document = request.document,
                .theme = request.theme,
                .baseFontSize = request.baseFontSize,
                .typefaces = request.typefaces,
                .layoutTypeface = request.layoutTypeface,
                .imageSizeProvider = request.imageSizeProvider,
                .pageWidth = request.pageWidth,
                .pageHeight = request.pageHeight,
            },
            prepared)) {
        return PdfExportStatus::WriteError;
    }

    SkDynamicMemoryWStream pdfBytes;
    sk_sp<SkDocument> pdfDocument = SkPDF::MakeDocument(&pdfBytes, MakePdfMetadata(request.sourcePath));
    if (!pdfDocument) {
        return PdfExportStatus::PdfBackendUnavailable;
    }

    for (size_t pageIndex = 0; pageIndex < prepared.pages.size(); ++pageIndex) {
        SkCanvas* canvas = pdfDocument->beginPage(request.pageWidth, request.pageHeight);
        if (!canvas) {
            pdfDocument->abort();
            return PdfExportStatus::WriteError;
        }

        RenderPrintDocumentPage(prepared, pageIndex, canvas, request.resolveImage);
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
