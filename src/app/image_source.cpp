#include "app/image_source.h"
#include "app/native_badge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

#include "util/path_utils.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mdviewer {
namespace {

bool IsNetworkPath(const std::filesystem::path& path) {
    const auto text = PathToUtf8(path);
    if (text.starts_with("//") || text.starts_with("\\\\")) return true;
#ifdef _WIN32
    // Drive type queries inspect the local drive mapping; do not stat a file
    // through a mapped network drive to decide whether consent is necessary.
    const auto root = path.root_path();
    if (!root.empty() && GetDriveTypeW(root.c_str()) == DRIVE_REMOTE) return true;
#endif
    return false;
}

bool HasScheme(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) return false;
#ifdef _WIN32
    if (colon == 1 && std::isalpha(static_cast<unsigned char>(text[0]))) return false;
#endif
    return true;
}

std::string DecodeMountField(std::string_view field) {
    std::string decoded;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size() && field[i + 1] >= '0' && field[i + 1] <= '7' &&
            field[i + 2] >= '0' && field[i + 2] <= '7' && field[i + 3] >= '0' && field[i + 3] <= '7') {
            decoded.push_back(static_cast<char>((field[i + 1] - '0') * 64 + (field[i + 2] - '0') * 8 + field[i + 3] - '0'));
            i += 3;
        } else decoded.push_back(field[i]);
    }
    return decoded;
}

bool NetworkFilesystem(std::string_view type) {
    return type == "nfs" || type == "nfs4" || type == "cifs" || type == "smb3" || type == "smbfs" ||
        type == "afs" || type == "ceph" || type == "9p" || type == "autofs" || type == "davfs" ||
        type == "fuse.sshfs" || type == "fuse.gvfsd-fuse" || type == "fuse.rclone" ||
        type == "fuse.davfs" || type == "fuse.smbnetfs" || type == "fuse.curlftpfs";
}

#ifndef _WIN32
std::string LinuxNetworkMountDescription(const std::filesystem::path& path) {
    std::ifstream mounts("/proc/self/mountinfo", std::ios::binary);
    if (!mounts) return "Mount information unavailable; explicit approval is required.";
    std::string text;
    char buffer[4096];
    while (mounts.read(buffer, sizeof(buffer)) || mounts.gcount() > 0) {
        if (text.size() + static_cast<size_t>(mounts.gcount()) > 4 * 1024 * 1024) {
            return "Mount information exceeds the inspection limit; explicit approval is required.";
        }
        text.append(buffer, static_cast<size_t>(mounts.gcount()));
    }
    if (mounts.bad()) return "Mount information unavailable; explicit approval is required.";
    return NetworkImageMountDescription(PathToUtf8(path), text);
}
#endif

} // namespace

std::string NetworkImageMountDescription(std::string_view path, std::string_view mountInfo) {
    std::istringstream lines{std::string(mountInfo)};
    std::string line, bestType, bestSource;
    size_t bestLength = 0;
    while (std::getline(lines, line)) {
        const auto separator = line.find(" - ");
        if (separator == std::string::npos) continue;
        std::istringstream left(line.substr(0, separator)), right(line.substr(separator + 3));
        std::string id, parent, device, root, point, type, source;
        if (!(left >> id >> parent >> device >> root >> point) || !(right >> type >> source)) continue;
        point = DecodeMountField(point);
        const bool contains = point == "/" || path == point ||
            (path.size() > point.size() && path.starts_with(point) && path[point.size()] == '/');
        if (contains && point.size() >= bestLength) {
            bestLength = point.size(); bestType = type; bestSource = DecodeMountField(source);
        }
    }
    if (!NetworkFilesystem(bestType)) return {};
    // Mount sources can include user:password@host. Never reveal credentials or
    // mount options in the document surface.
    if (const auto at = bestSource.rfind('@'); at != std::string::npos) {
        const bool unc = bestSource.starts_with("//");
        bestSource.erase(0, at + 1);
        if (unc) bestSource.insert(0, "//");
    }
    return "Network mount: " + bestSource + " (" + bestType + ")";
}

ImageSource DescribeImageSource(const std::string& source, const std::filesystem::path& baseDir) {
    ImageSource result;
    result.info.originalSource = source;
    result.info.displayTarget = source;
    if (ParseNativeBadge(source)) {
        result.info.kind = ImageSourceKind::NativeBadge;
        result.info.state = ImageLoadState::Ready;
        result.cacheKey = "badge:" + source;
        return result;
    }
    result.info.error = "Unsupported image source.";
    if (source.empty() || std::any_of(source.begin(), source.end(), [](unsigned char ch) {
            return ch < 0x20 || ch == 0x7f;
        })) return result;

    std::string lowered = source;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lowered.starts_with("http://") || lowered.starts_with("https://")) {
        const auto authorityStart = source.find("://") + 3;
        const auto authorityEnd = source.find_first_of("/?#", authorityStart);
        const auto authority = source.substr(authorityStart, authorityEnd - authorityStart);
        if (authority.empty() || authority.find('@') != std::string::npos ||
            authority.find('\\') != std::string::npos) {
            result.info.error = "Image URLs containing credentials or an invalid host are not supported.";
            return result;
        }
        result.info.kind = ImageSourceKind::RemoteUrl;
        result.info.state = ImageLoadState::Blocked;
        result.info.error.clear();
        result.cacheKey = "url:" + source;
        return result;
    }

    std::string pathSource = source;
    if (lowered.starts_with("file://")) {
        pathSource = source.substr(7);
        if (!pathSource.starts_with('/')) {
            const auto slash = pathSource.find('/');
            const auto host = pathSource.substr(0, slash);
            if (host == "localhost") pathSource = slash == std::string::npos ? "" : pathSource.substr(slash);
            else pathSource = "//" + pathSource;
        }
#ifdef _WIN32
        if (pathSource.size() > 2 && pathSource[0] == '/' && pathSource[2] == ':') pathSource.erase(0, 1);
#endif
    } else if (HasScheme(pathSource)) {
        return result;
    }
    const auto decoded = PercentDecodePath(pathSource);
    if (!decoded || decoded->empty()) return result;
    if (decoded->starts_with("\\\\?\\") || decoded->starts_with("\\\\.\\") ||
        decoded->starts_with("//?/") || decoded->starts_with("//./")) {
        result.info.error = "Device paths cannot be used as image sources.";
        return result;
    }
    try {
        auto path = PathFromUtf8(*decoded);
        if (path.is_relative()) path = baseDir / path;
        const bool networkPath = IsNetworkPath(path);
        result.resolvedPath = NormalizeDocumentPath(path);
        result.info.displayTarget = PathToUtf8(result.resolvedPath);
        result.cacheKey = "file:" + result.info.displayTarget;
        result.info.kind = networkPath || IsNetworkPath(result.resolvedPath) ? ImageSourceKind::NetworkPath : ImageSourceKind::Local;
#ifndef _WIN32
        if (result.info.kind == ImageSourceKind::Local) {
            const auto mount = LinuxNetworkMountDescription(result.resolvedPath);
            if (!mount.empty()) {
                result.info.kind = ImageSourceKind::NetworkPath;
                result.info.displayTarget += "\n" + mount;
            }
        }
#endif
        result.info.state = result.info.RequiresConsent() ? ImageLoadState::Blocked : ImageLoadState::Local;
        result.info.error.clear();
    } catch (...) {
        result.info.error = "Invalid image path.";
    }
    return result;
}

} // namespace mdviewer
