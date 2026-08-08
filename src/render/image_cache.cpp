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

constexpr int kMaxDecodedImageDimension = 8192;
constexpr uint64_t kMaxDecodedImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr int kMaxScaledImageDimension = 8192;
constexpr uint64_t kMaxScaledImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxCachedImageEntries = 256;
constexpr size_t kMaxScaledImageBytes = 128ULL * 1024ULL * 1024ULL;

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
    scaledImageBytes_ = 0;
}

void DocumentImageCache::ClearScaledImages() {
    for (auto& [key, entry] : entries_) {
        (void)key;
        entry.scaledImages.clear();
    }
    scaledImageBytes_ = 0;
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
    if (!baseImage || !std::isfinite(displayWidth) || !std::isfinite(displayHeight) ||
        displayWidth <= 0.0f || displayHeight <= 0.0f) {
        return nullptr;
    }

    double safeWidth = displayWidth;
    double safeHeight = displayHeight;
    const double dimensionScale = std::min(
        1.0,
        std::min(
            static_cast<double>(kMaxScaledImageDimension) / safeWidth,
            static_cast<double>(kMaxScaledImageDimension) / safeHeight));
    safeWidth *= dimensionScale;
    safeHeight *= dimensionScale;
    const double pixelCount = safeWidth * safeHeight;
    if (!std::isfinite(pixelCount) || pixelCount <= 0.0) {
        return nullptr;
    }
    if (pixelCount > static_cast<double>(kMaxScaledImagePixels)) {
        const double pixelScale = std::sqrt(static_cast<double>(kMaxScaledImagePixels) / pixelCount);
        safeWidth *= pixelScale;
        safeHeight *= pixelScale;
    }

    const int targetWidth = std::max(1, static_cast<int>(std::round(safeWidth)));
    const int targetHeight = std::max(1, static_cast<int>(std::round(safeHeight)));
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
        const uint64_t byteCount = static_cast<uint64_t>(targetWidth) *
            static_cast<uint64_t>(targetHeight) * 4ULL;
        if (byteCount <= kMaxScaledImageBytes) {
            if (scaledImageBytes_ > kMaxScaledImageBytes - static_cast<size_t>(byteCount)) {
                ClearScaledImages();
            }
            auto& refreshedEntry = entries_[MakeCacheKey(imagePath)];
            refreshedEntry.scaledImages[scaledKey] = scaledImage;
            scaledImageBytes_ += static_cast<size_t>(byteCount);
        }
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
    std::error_code existsError;
    if (!std::filesystem::exists(imagePath, existsError) || existsError) {
        return nullptr;
    }

    auto data = SkData::MakeFromFileName(imagePath.string().c_str());
    if (!data) {
        return nullptr;
    }

    auto image = SkImages::DeferredFromEncodedData(data);
    if (!image || image->width() <= 0 || image->height() <= 0 ||
        image->width() > kMaxDecodedImageDimension || image->height() > kMaxDecodedImageDimension ||
        static_cast<uint64_t>(image->width()) * static_cast<uint64_t>(image->height()) > kMaxDecodedImagePixels) {
        return nullptr;
    }

    return image->makeRasterImage();
}

sk_sp<SkImage> DocumentImageCache::GetOrLoadBaseImage(const std::string& url, const std::filesystem::path& baseDir) {
    const std::filesystem::path imagePath = ResolveImagePath(url, baseDir);
    const std::string key = MakeCacheKey(imagePath);
    if (const auto existing = entries_.find(key); existing != entries_.end()) {
        return existing->second.baseImage;
    }

    sk_sp<SkImage> image = CreateRasterImageFromFile(imagePath);
    if (image && entries_.size() < kMaxCachedImageEntries) {
        entries_.emplace(key, CachedImageEntry{image, {}});
    }
    return image;
}

} // namespace mdviewer
