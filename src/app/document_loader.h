#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "layout/document_model.h"

namespace mdviewer {

inline constexpr uintmax_t kMaxDocumentFileSizeBytes = 64ULL * 1024ULL * 1024ULL;

enum class DocumentLoadStatus {
    Success,
    FileReadError,
    FileTooLarge,
    BinaryFile
};

struct DocumentLoadResult {
    DocumentLoadStatus status = DocumentLoadStatus::FileReadError;
    std::string sourceText;
    DocumentModel docModel;
};

bool IsMarkdownFile(const std::filesystem::path& path);
bool IsDefinitelyTextFile(const std::filesystem::path& path);
bool IsKnownNonTextFile(const std::filesystem::path& path);
bool ProbeIsText(const std::string& content);
DocumentLoadResult LoadDocumentFromPath(const std::filesystem::path& path);

} // namespace mdviewer
