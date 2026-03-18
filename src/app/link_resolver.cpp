#include "app/link_resolver.h"

#include <sstream>

#include "app/document_loader.h"
#include "util/file_io.h"

namespace mdviewer {

namespace {

std::string UrlDecode(const std::string& text) {
    std::string decoded;
    for (size_t index = 0; index < text.length(); ++index) {
        if (text[index] == '%' && index + 2 < text.length()) {
            int value = 0;
            std::stringstream stream;
            stream << std::hex << text.substr(index + 1, 2);
            stream >> value;
            decoded += static_cast<char>(value);
            index += 2;
        } else if (text[index] == '+') {
            decoded += ' ';
        } else {
            decoded += text[index];
        }
    }
    return decoded;
}

bool ShouldOpenInternally(const std::filesystem::path& path, bool forceExternal) {
    if (forceExternal) {
        return false;
    }
    if (IsMarkdownFile(path) || IsDefinitelyTextFile(path)) {
        return true;
    }

    auto content = ReadFileToString(path);
    return content && ProbeIsText(*content);
}

} // namespace

LinkTarget ResolveLinkTarget(
    const std::filesystem::path& currentFilePath,
    const std::string& url,
    bool forceExternal) {
    if (url.empty()) {
        return {};
    }

    std::string pathPart = url;
    const size_t hashPos = pathPart.find('#');
    if (hashPos != std::string::npos) {
        pathPart = pathPart.substr(0, hashPos);
    }

    const size_t queryPos = pathPart.find('?');
    if (queryPos != std::string::npos) {
        pathPart = pathPart.substr(0, queryPos);
    }

    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return {LinkTargetKind::ExternalUrl, url, {}};
    }

    if (url.rfind("file://", 0) == 0) {
        std::string localPath = UrlDecode(url.substr(7));
        if (localPath.size() > 2 && localPath[0] == '/' && localPath[2] == ':') {
            localPath = localPath.substr(1);
        }

        const size_t fragmentPos = localPath.find('#');
        if (fragmentPos != std::string::npos) {
            localPath = localPath.substr(0, fragmentPos);
        }

        std::filesystem::path targetPath(localPath);
        if (ShouldOpenInternally(targetPath, forceExternal)) {
            return {LinkTargetKind::InternalDocument, {}, targetPath};
        }
        return {LinkTargetKind::ExternalPath, {}, targetPath};
    }

    std::filesystem::path targetPath = currentFilePath.parent_path() / UrlDecode(pathPart);
    if (!std::filesystem::exists(targetPath)) {
        targetPath = std::filesystem::path(UrlDecode(pathPart));
    }

    if (!std::filesystem::exists(targetPath)) {
        return {};
    }

    if (ShouldOpenInternally(targetPath, forceExternal)) {
        return {LinkTargetKind::InternalDocument, {}, targetPath};
    }
    return {LinkTargetKind::ExternalPath, {}, targetPath};
}

} // namespace mdviewer
