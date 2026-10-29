#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace mdviewer {

enum class ImageSourceKind { Local, RemoteUrl, NetworkPath, Invalid, NativeBadge };
enum class ImageLoadState { Local, Blocked, Loading, Ready, Failed };

struct ImageLoadInfo {
    ImageSourceKind kind = ImageSourceKind::Invalid;
    ImageLoadState state = ImageLoadState::Failed;
    std::string originalSource;
    std::string displayTarget;
    std::string error;

    [[nodiscard]] bool RequiresConsent() const {
        return kind == ImageSourceKind::RemoteUrl || kind == ImageSourceKind::NetworkPath;
    }
};

struct ImageSource {
    ImageLoadInfo info;
    std::filesystem::path resolvedPath;
    std::string cacheKey;
};

// Classification never opens the referenced file or probes a network resource.
ImageSource DescribeImageSource(const std::string& source, const std::filesystem::path& baseDir);

// Pure mountinfo parser, also used by tests without mounting/probing a share.
// Returns a credential-free description of a network/automount containing path.
std::string NetworkImageMountDescription(std::string_view path, std::string_view mountInfo);

} // namespace mdviewer
