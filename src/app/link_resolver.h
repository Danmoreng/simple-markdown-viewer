#pragma once

#include <filesystem>
#include <string>

namespace mdviewer {

enum class LinkTargetKind {
    Invalid,
    InternalDocument,
    ExternalUrl,
    ExternalPath
};

struct LinkTarget {
    LinkTargetKind kind = LinkTargetKind::Invalid;
    std::string externalUrl;
    std::filesystem::path path;
};

LinkTarget ResolveLinkTarget(
    const std::filesystem::path& currentFilePath,
    const std::string& url,
    bool forceExternal);

} // namespace mdviewer
