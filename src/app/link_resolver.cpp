#include "app/link_resolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

#include "app/document_loader.h"

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

bool ProbeFileIsText(const std::filesystem::path& path) {
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
    std::string extension = path.extension().string();
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
    bool forceExternal) {
    if (url.empty()) {
        return {};
    }

    std::string pathPart = url;
    std::string fragment;
    const size_t hashPos = pathPart.find('#');
    if (hashPos != std::string::npos) {
        fragment = UrlDecode(pathPart.substr(hashPos + 1));
        pathPart = pathPart.substr(0, hashPos);
    }

    const size_t queryPos = pathPart.find('?');
    if (queryPos != std::string::npos) {
        pathPart = pathPart.substr(0, queryPos);
    }

    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return {LinkTargetKind::ExternalUrl, url, {}, {}};
    }

    if (url.rfind("file://", 0) == 0) {
        std::string localPath = UrlDecode(url.substr(7));
        if (localPath.size() > 2 && localPath[0] == '/' && localPath[2] == ':') {
            localPath = localPath.substr(1);
        }

        const size_t fragmentPos = localPath.find('#');
        if (fragmentPos != std::string::npos) {
            fragment = UrlDecode(localPath.substr(fragmentPos + 1));
            localPath = localPath.substr(0, fragmentPos);
        }

        std::filesystem::path targetPath(localPath);
        if (!std::filesystem::exists(targetPath)) {
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

    const std::filesystem::path decodedPath = std::filesystem::path(UrlDecode(pathPart));
    const std::filesystem::path documentRelativePath = currentFilePath.parent_path() / decodedPath;
    std::filesystem::path targetPath = documentRelativePath;
    if (!std::filesystem::exists(targetPath)) {
        targetPath = std::filesystem::path(UrlDecode(pathPart));
    }

    if (!std::filesystem::exists(targetPath)) {
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
