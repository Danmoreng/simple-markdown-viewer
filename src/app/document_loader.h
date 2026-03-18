#pragma once

#include <filesystem>
#include <string>

#include "layout/document_model.h"

namespace mdviewer {

enum class DocumentLoadStatus {
    Success,
    FileReadError,
    BinaryFile
};

struct DocumentLoadResult {
    DocumentLoadStatus status = DocumentLoadStatus::FileReadError;
    std::string sourceText;
    DocumentModel docModel;
};

bool IsMarkdownFile(const std::filesystem::path& path);
bool IsDefinitelyTextFile(const std::filesystem::path& path);
bool ProbeIsText(const std::string& content);
DocumentLoadResult LoadDocumentFromPath(const std::filesystem::path& path);

} // namespace mdviewer
