#include "platform/linux/linux_window_icon.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <limits.h>
#include <unistd.h>

#include "GLFW/glfw3.h"

#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#pragma warning(pop)

namespace mdviewer::linux_platform {
namespace {

std::filesystem::path GetExecutableDirectory() {
    char pathBuffer[PATH_MAX];
    const ssize_t pathLength = readlink("/proc/self/exe", pathBuffer, sizeof(pathBuffer));
    if (pathLength <= 0) {
        return {};
    }
    return std::filesystem::path(std::string(pathBuffer, static_cast<size_t>(pathLength))).parent_path();
}

std::vector<std::filesystem::path> GetIconCandidates() {
    const std::filesystem::path executableDirectory = GetExecutableDirectory();
    return {
        executableDirectory / ".." / "share" / "pixmaps" / "mdviewer.png",
        executableDirectory / ".." / "resources" / "linux" / "mdviewer.png",
        std::filesystem::current_path() / "resources" / "linux" / "mdviewer.png",
    };
}

bool TrySetWindowIcon(GLFWwindow* window, const std::filesystem::path& iconPath) {
    std::error_code existsError;
    if (!std::filesystem::exists(iconPath, existsError) || existsError) {
        return false;
    }

    const auto data = SkData::MakeFromFileName(iconPath.string().c_str());
    const auto encodedImage = data ? SkImages::DeferredFromEncodedData(data) : nullptr;
    if (!encodedImage || encodedImage->width() <= 0 || encodedImage->height() <= 0 ||
        encodedImage->width() > 1024 || encodedImage->height() > 1024) {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(encodedImage->width()) * 4;
    std::vector<uint8_t> pixels(rowBytes * static_cast<size_t>(encodedImage->height()));
    const SkImageInfo imageInfo = SkImageInfo::Make(
        encodedImage->width(),
        encodedImage->height(),
        kRGBA_8888_SkColorType,
        kUnpremul_SkAlphaType);
    if (!encodedImage->readPixels(nullptr, imageInfo, pixels.data(), rowBytes, 0, 0)) {
        return false;
    }

    GLFWimage icon{
        encodedImage->width(),
        encodedImage->height(),
        pixels.data(),
    };
    glfwSetWindowIcon(window, 1, &icon);
    return true;
}

} // namespace

bool SetLinuxWindowIcon(GLFWwindow* window) {
    if (window == nullptr) {
        return false;
    }
    for (const auto& iconPath : GetIconCandidates()) {
        if (TrySetWindowIcon(window, iconPath)) {
            return true;
        }
    }
    return false;
}

} // namespace mdviewer::linux_platform
