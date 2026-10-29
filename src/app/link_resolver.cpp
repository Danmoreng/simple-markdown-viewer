#include "app/link_resolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string_view>

#include "app/document_loader.h"
#include "util/path_utils.h"

namespace mdviewer {

namespace {

bool PathExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

bool ProbeFileIsText(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }

    std::array<char, 4096> buffer{};
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    return ProbeIsText(std::string(buffer.data(), static_cast<size_t>(stream.gcount())));
}

bool ShouldOpenInternally(const std::filesystem::path& path, bool forceExternal) {
    if (forceExternal || IsKnownNonTextFile(path)) {
        return false;
    }
    if (IsMarkdownFile(path) || IsDefinitelyTextFile(path)) {
        return true;
    }

    return ProbeFileIsText(path);
}

bool HasUnsupportedScheme(const std::string& path) {
    const size_t colon = path.find(':');
    if (colon == std::string::npos) {
        return false;
    }

#ifdef _WIN32
    if (colon == 1 && std::isalpha(static_cast<unsigned char>(path[0]))) {
        return false;
    }
#endif

    if (colon == 0 || !std::isalpha(static_cast<unsigned char>(path[0]))) {
        return false;
    }
    for (size_t index = 1; index < colon; ++index) {
        const auto ch = static_cast<unsigned char>(path[index]);
        if (!std::isalnum(ch) && ch != '+' && ch != '-' && ch != '.') {
            return false;
        }
    }
    return true;
}

bool HasExecutableExtension(const std::filesystem::path& path) {
    std::string extension = PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    constexpr std::array<std::string_view, 36> executableExtensions = {
        ".app", ".appimage", ".bat", ".cmd", ".com", ".command", ".cpl", ".desktop", ".exe", ".gadget",
        ".hta", ".inf", ".jar", ".js", ".jse", ".lnk", ".msc", ".msi", ".msp", ".mst", ".pif",
        ".ps1", ".reg", ".run", ".scf", ".scr", ".sct", ".sh", ".shb", ".shs", ".vbe", ".vbs",
        ".ws", ".wsf", ".wsh", ".url",
    };
    return std::find(executableExtensions.begin(), executableExtensions.end(), extension) != executableExtensions.end();
}

bool HasExecutablePermissions(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    return false;
#else
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    const auto permissions = status.permissions();
    if (error || !std::filesystem::is_regular_file(status) || permissions == std::filesystem::perms::unknown) {
        return false;
    }
    constexpr auto executePermissions =
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec;
    return (permissions & executePermissions) != std::filesystem::perms::none;
#endif
}

LinkTarget MakeExistingLocalTarget(
    LinkTargetKind kind,
    const std::filesystem::path& targetPath,
    std::string fragment) {
    LinkTarget target{kind, {}, targetPath, std::move(fragment)};
    target.executableLocalFile = HasExecutableExtension(targetPath) || HasExecutablePermissions(targetPath);
    return target;
}

} // namespace

LinkTarget ResolveLinkTarget(
    const std::filesystem::path& currentFilePath,
    const std::string& url,
    bool forceExternal,
    bool inspectFileSystem) {
    if (url.empty() || url.find('\0') != std::string::npos) {
        return {};
    }

    std::string lowerPrefix = url.substr(0, 8);
    std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lowerPrefix.starts_with("http://") || lowerPrefix.starts_with("https://")) {
        return {LinkTargetKind::ExternalUrl, url, {}, {}};
    }

    std::string pathPart = url;
    std::string fragment;
    const size_t hashPos = pathPart.find('#');
    if (hashPos != std::string::npos) {
        auto decoded = PercentDecodePath(std::string_view(pathPart).substr(hashPos + 1));
        if (!decoded) return {};
        fragment = std::move(*decoded);
        pathPart = pathPart.substr(0, hashPos);
    }

    const size_t queryPos = pathPart.find('?');
    if (queryPos != std::string::npos) {
        pathPart = pathPart.substr(0, queryPos);
    }

    if (lowerPrefix.starts_with("file://")) {
        std::string rawPath = pathPart.substr(7);
        // Split URL structure before decoding: %23 is part of a filename.
        if (rawPath.starts_with("localhost/")) rawPath.erase(0, 9);
        else if (!rawPath.empty() && rawPath.front() != '/' &&
                 !(rawPath.size() >= 2 && rawPath[1] == ':')) rawPath = "//" + rawPath;
        auto decoded = PercentDecodePath(rawPath);
        if (!decoded) return {};
        std::string localPath = std::move(*decoded);
#ifdef _WIN32
        if (localPath.size() > 2 && localPath[0] == '/' && localPath[2] == ':') {
            localPath = localPath.substr(1);
        }
#endif
        std::filesystem::path targetPath = PathFromUtf8(localPath);
        if (!inspectFileSystem) {
            return {LinkTargetKind::ExternalPath, {}, targetPath.lexically_normal(), fragment,
                HasExecutableExtension(targetPath)};
        }
        if (!PathExists(targetPath)) {
            return {LinkTargetKind::MissingLocalPath, {}, targetPath, fragment};
        }
        if (ShouldOpenInternally(targetPath, forceExternal)) {
            return MakeExistingLocalTarget(
                LinkTargetKind::InternalDocument,
                targetPath,
                std::move(fragment));
        }
        return MakeExistingLocalTarget(
            LinkTargetKind::ExternalPath,
            targetPath,
            {});
    }

    if (pathPart.empty() && !fragment.empty() && !currentFilePath.empty() && !forceExternal) {
        return {LinkTargetKind::InternalDocument, {}, currentFilePath, fragment};
    }

    if (HasUnsupportedScheme(pathPart)) {
        return {};
    }

    const auto decoded = PercentDecodePath(pathPart);
    if (!decoded || HasUnsupportedScheme(*decoded)) return {};
    const std::filesystem::path decodedPath = PathFromUtf8(*decoded);
    const std::filesystem::path documentRelativePath = currentFilePath.parent_path() / decodedPath;
    if (!inspectFileSystem) {
        return {LinkTargetKind::ExternalPath, {}, documentRelativePath.lexically_normal(), fragment,
            HasExecutableExtension(documentRelativePath)};
    }
    std::filesystem::path targetPath = documentRelativePath;
    if (!PathExists(targetPath)) {
        targetPath = decodedPath;
    }

    if (!PathExists(targetPath)) {
        return {LinkTargetKind::MissingLocalPath, {}, documentRelativePath, fragment};
    }

    if (ShouldOpenInternally(targetPath, forceExternal)) {
        return MakeExistingLocalTarget(
            LinkTargetKind::InternalDocument,
            targetPath,
            std::move(fragment));
    }
    return MakeExistingLocalTarget(
        LinkTargetKind::ExternalPath,
        targetPath,
        {});
}

} // namespace mdviewer
