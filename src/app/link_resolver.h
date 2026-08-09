#pragma once

#include <filesystem>
#include <string>

namespace mdviewer {

enum class LinkTargetKind {
    Invalid,
    MissingLocalPath,
    InternalDocument,
    ExternalUrl,
    ExternalPath
};

struct LinkTarget {
    LinkTargetKind kind = LinkTargetKind::Invalid;
    std::string externalUrl;
    std::filesystem::path path;
    std::string fragment;
    bool executableLocalFile = false;

    [[nodiscard]] bool RequiresConfirmation() const {
        return executableLocalFile;
    }
};

LinkTarget ResolveLinkTarget(
    const std::filesystem::path& currentFilePath,
    const std::string& url,
    bool forceExternal);

} // namespace mdviewer
