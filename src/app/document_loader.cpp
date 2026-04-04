#include "app/document_loader.h"

#include <algorithm>
#include <cctype>

#include "markdown/markdown_parser.h"
#include "util/file_io.h"
#include "util/utf8.h"

namespace mdviewer {

bool IsMarkdownFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".md" || ext == ".markdown";
}

bool IsDefinitelyTextFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".txt" || ext == ".log" || ext == ".ini" || ext == ".conf" || ext == ".json" || ext == ".xml" || ext == ".yml" || ext == ".yaml") {
        return true;
    }

    std::string name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name == "license" ||
           name == "license.txt" ||
           name == "copying" ||
           name == "notice" ||
           name == "third_party_notices" ||
           name == "readme";
}

bool ProbeIsText(const std::string& content) {
    const size_t checkSize = std::min(content.size(), static_cast<size_t>(4096));
    for (size_t index = 0; index < checkSize; ++index) {
        if (content[index] == '\0') {
            return false;
        }
    }
    return true;
}

DocumentLoadResult LoadDocumentFromPath(const std::filesystem::path& path) {
    auto content = ReadFileToString(path);
    if (!content) {
        return {DocumentLoadStatus::FileReadError, {}, {}};
    }

    Utf8SanitizationResult sanitized = SanitizeUtf8(*content);
    DocumentModel docModel;
    if (IsMarkdownFile(path)) {
        docModel = MarkdownParser::Parse(sanitized.text);
    } else if (IsDefinitelyTextFile(path) || ProbeIsText(*content)) {
        Block block;
        block.type = BlockType::Paragraph;
        block.inlineRuns.push_back({InlineStyle::Plain, sanitized.text, ""});
        docModel.blocks.push_back(std::move(block));
    } else {
        return {DocumentLoadStatus::BinaryFile, {}, {}};
    }

    return {DocumentLoadStatus::Success, std::move(sanitized.text), std::move(docModel)};
}

} // namespace mdviewer
