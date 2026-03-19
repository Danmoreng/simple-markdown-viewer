#include "render/image_cache.h"

#include <algorithm>
#include <cmath>

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#pragma warning(pop)

namespace mdviewer {

namespace {

void PreloadBlocks(
    DocumentImageCache& cache,
    const std::vector<Block>& blocks,
    const std::filesystem::path& baseDir) {
    for (const auto& block : blocks) {
        for (const auto& run : block.inlineRuns) {
            if (run.style == InlineStyle::Image) {
                cache.GetImageSize(run.url, baseDir);
            }
        }

        if (!block.children.empty()) {
            PreloadBlocks(cache, block.children, baseDir);
        }
    }
}

} // namespace

void DocumentImageCache::Clear() {
    entries_.clear();
}

std::pair<float, float> DocumentImageCache::GetImageSize(const std::string& url, const std::filesystem::path& baseDir) {
    const sk_sp<SkImage> image = GetOrLoadBaseImage(url, baseDir);
    if (!image) {
        return {0.0f, 0.0f};
    }

    return {static_cast<float>(image->width()), static_cast<float>(image->height())};
}

sk_sp<SkImage> DocumentImageCache::GetImage(
    const std::string& url,
    const std::filesystem::path& baseDir,
    float displayWidth,
    float displayHeight) {
    sk_sp<SkImage> baseImage = GetOrLoadBaseImage(url, baseDir);
    if (!baseImage) {
        return nullptr;
    }

    const int targetWidth = std::max(1, static_cast<int>(std::round(displayWidth)));
    const int targetHeight = std::max(1, static_cast<int>(std::round(displayHeight)));
    if (baseImage->width() == targetWidth && baseImage->height() == targetHeight) {
        return baseImage;
    }

    const std::filesystem::path imagePath = ResolveImagePath(url, baseDir);
    auto& entry = entries_[MakeCacheKey(imagePath)];
    const uint64_t scaledKey = MakeScaledImageKey(static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    auto it = entry.scaledImages.find(scaledKey);
    if (it != entry.scaledImages.end()) {
        return it->second;
    }

    const auto info = SkImageInfo::MakeN32Premul(targetWidth, targetHeight);
    auto surface = SkSurfaces::Raster(info);
    if (!surface) {
        return baseImage;
    }

    SkCanvas* scaleCanvas = surface->getCanvas();
    scaleCanvas->clear(SK_ColorTRANSPARENT);
    scaleCanvas->drawImageRect(
        baseImage,
        SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight)),
        SkSamplingOptions(SkFilterMode::kLinear));

    auto scaledImage = surface->makeImageSnapshot();
    if (scaledImage) {
        scaledImage = scaledImage->makeRasterImage();
    }
    if (scaledImage) {
        entry.scaledImages[scaledKey] = scaledImage;
        return scaledImage;
    }

    return baseImage;
}

void DocumentImageCache::PreloadDocumentImages(const DocumentModel& doc, const std::filesystem::path& baseDir) {
    PreloadBlocks(*this, doc.blocks, baseDir);
}

std::filesystem::path DocumentImageCache::ResolveImagePath(
    const std::string& url,
    const std::filesystem::path& baseDir) {
    const std::u8string utf8Url(url.begin(), url.end());
    std::filesystem::path imagePath(utf8Url);
    if (imagePath.is_relative()) {
        imagePath = baseDir / imagePath;
    }

    try {
        return std::filesystem::absolute(imagePath).lexically_normal();
    } catch (...) {
        return imagePath.lexically_normal();
    }
}

std::string DocumentImageCache::MakeCacheKey(const std::filesystem::path& imagePath) {
    const auto nativeKey = imagePath.generic_u8string();
    return std::string(nativeKey.begin(), nativeKey.end());
}

uint64_t DocumentImageCache::MakeScaledImageKey(float width, float height) {
    const uint32_t roundedWidth = static_cast<uint32_t>(std::max(1.0f, std::round(width)));
    const uint32_t roundedHeight = static_cast<uint32_t>(std::max(1.0f, std::round(height)));
    return (static_cast<uint64_t>(roundedWidth) << 32) | roundedHeight;
}

sk_sp<SkImage> DocumentImageCache::CreateRasterImageFromFile(const std::filesystem::path& imagePath) {
    if (!std::filesystem::exists(imagePath)) {
        return nullptr;
    }

    auto data = SkData::MakeFromFileName(imagePath.string().c_str());
    if (!data) {
        return nullptr;
    }

    auto image = SkImages::DeferredFromEncodedData(data);
    if (!image) {
        return nullptr;
    }

    return image->makeRasterImage();
}

sk_sp<SkImage> DocumentImageCache::GetOrLoadBaseImage(const std::string& url, const std::filesystem::path& baseDir) {
    const std::filesystem::path imagePath = ResolveImagePath(url, baseDir);
    auto& entry = entries_[MakeCacheKey(imagePath)];
    if (entry.baseImage) {
        return entry.baseImage;
    }

    entry.baseImage = CreateRasterImageFromFile(imagePath);
    return entry.baseImage;
}

} // namespace mdviewer
