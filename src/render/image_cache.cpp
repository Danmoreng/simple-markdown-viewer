#include "render/image_cache.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

#include "util/skia_font_utils.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#pragma warning(pop)

namespace mdviewer {

namespace {

constexpr int kMaxDecodedImageDimension = 8192;
constexpr uint64_t kMaxDecodedImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr int kMaxScaledImageDimension = 8192;
constexpr uint64_t kMaxScaledImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxCachedImageEntries = 256;
constexpr size_t kMaxScaledImageBytes = 128ULL * 1024ULL * 1024ULL;
constexpr uintmax_t kMaxSvgFileBytes = 8ULL * 1024ULL * 1024ULL;

bool IsSvgPath(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".svg";
}

bool IsRemoteImageUrl(std::string_view url) {
    std::string lowered(url);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lowered.starts_with("https://") || lowered.starts_with("http://");
}

void PreloadBlocks(
    DocumentImageCache& cache,
    const std::vector<Block>& blocks,
    const std::filesystem::path& baseDir) {
    for (const auto& block : blocks) {
        for (const auto& run : block.inlineRuns) {
            if (run.kind == InlineKind::Image) {
                cache.GetImageSize(run.imageSource, baseDir);
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
    liveResize_ = false;
}

void DocumentImageCache::BeginLiveResize() {
    liveResize_ = true;
    for (auto& [key, entry] : entries_) {
        (void)key;
        entry.liveResizeImage.reset();
        if (entry.lastScaledImageKey != 0) {
            const auto lastImage = entry.scaledImages.find(entry.lastScaledImageKey);
            if (lastImage != entry.scaledImages.end()) {
                entry.liveResizeImage = lastImage->second;
            }
        }
        if (!entry.liveResizeImage && entry.baseImage) {
            entry.liveResizeImage = entry.baseImage;
        }
        if (!entry.liveResizeImage && !entry.scaledImages.empty()) {
            entry.liveResizeImage = entry.scaledImages.rbegin()->second;
        }
    }
}

void DocumentImageCache::EndLiveResize() {
    liveResize_ = false;
    for (auto& [key, entry] : entries_) {
        (void)key;
        entry.liveResizeImage.reset();
    }
}

void DocumentImageCache::ClearScaledImages() {
    for (auto& [key, entry] : entries_) {
        (void)key;
        entry.scaledImages.clear();
        entry.lastScaledImageKey = 0;
    }
    scaledImageBytes_ = 0;
}

std::pair<float, float> DocumentImageCache::GetImageSize(const std::string& url, const std::filesystem::path& baseDir) {
    if (IsRemoteImageUrl(url)) {
        return {0.0f, 0.0f};
    }
    const std::filesystem::path imagePath = ResolveImagePath(url, baseDir);
    CachedImageEntry* entry = GetOrLoadEntry(imagePath);
    if (!entry) {
        return {0.0f, 0.0f};
    }
    if (entry->svgDom) {
        return {entry->intrinsicSize.width(), entry->intrinsicSize.height()};
    }
    if (entry->baseImage) {
        return {static_cast<float>(entry->baseImage->width()), static_cast<float>(entry->baseImage->height())};
    }
    return {0.0f, 0.0f};
}

sk_sp<SkImage> DocumentImageCache::GetImage(
    const std::string& url,
    const std::filesystem::path& baseDir,
    float displayWidth,
    float displayHeight) {
    if (IsRemoteImageUrl(url)) {
        return nullptr;
    }
    const std::filesystem::path imagePath = ResolveImagePath(url, baseDir);
    CachedImageEntry* entry = GetOrLoadEntry(imagePath);
    if (!entry || (!entry->baseImage && !entry->svgDom) ||
        !std::isfinite(displayWidth) || !std::isfinite(displayHeight) ||
        displayWidth <= 0.0f || displayHeight <= 0.0f) {
        return nullptr;
    }

    if (liveResize_) {
        return entry->liveResizeImage;
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
    if (entry->baseImage && entry->baseImage->width() == targetWidth && entry->baseImage->height() == targetHeight) {
        entry->lastScaledImageKey = 0;
        return entry->baseImage;
    }

    const uint64_t scaledKey = MakeScaledImageKey(static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    auto it = entry->scaledImages.find(scaledKey);
    if (it != entry->scaledImages.end()) {
        entry->lastScaledImageKey = scaledKey;
        return it->second;
    }

    if (entry->svgDom) {
        auto image = RenderSvg(*entry, targetWidth, targetHeight);
        if (image) {
            entry->lastScaledImageKey = scaledKey;
        }
        return image;
    }

    sk_sp<SkImage> baseImage = entry->baseImage;

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
        entry->lastScaledImageKey = scaledKey;
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

DocumentImageCache::CachedImageEntry* DocumentImageCache::GetOrLoadEntry(
    const std::filesystem::path& imagePath) {
    const std::string key = MakeCacheKey(imagePath);
    if (auto existing = entries_.find(key); existing != entries_.end()) {
        return &existing->second;
    }
    if (entries_.size() >= kMaxCachedImageEntries) {
        return nullptr;
    }

    CachedImageEntry entry;
    if (IsSvgPath(imagePath)) {
        std::error_code sizeError;
        const uintmax_t fileSize = std::filesystem::file_size(imagePath, sizeError);
        if (!sizeError && fileSize <= kMaxSvgFileBytes) {
            SkFILEStream stream(imagePath.string().c_str());
            if (stream.isValid()) {
                if (!svgFontManager_) {
                    svgFontManager_ = CreateFontManager();
                }
                SkSVGDOM::Builder builder;
                builder.setFontManager(svgFontManager_);
                builder.setTextShapingFactory(SkShapers::Primitive::Factory());
                entry.svgDom = builder.make(stream);
                if (entry.svgDom) {
                    entry.intrinsicSize = entry.svgDom->containerSize();
                    const double pixelCount = static_cast<double>(entry.intrinsicSize.width()) *
                        static_cast<double>(entry.intrinsicSize.height());
                    if (!std::isfinite(pixelCount) || entry.intrinsicSize.width() <= 0.0f ||
                        entry.intrinsicSize.height() <= 0.0f ||
                        entry.intrinsicSize.width() > kMaxDecodedImageDimension ||
                        entry.intrinsicSize.height() > kMaxDecodedImageDimension ||
                        pixelCount > static_cast<double>(kMaxDecodedImagePixels)) {
                        entry.svgDom.reset();
                        entry.intrinsicSize = SkSize::Make(0.0f, 0.0f);
                    }
                }
            }
        }
    } else {
        entry.baseImage = CreateRasterImageFromFile(imagePath);
    }

    auto [inserted, unused] = entries_.emplace(key, std::move(entry));
    (void)unused;
    return &inserted->second;
}

sk_sp<SkImage> DocumentImageCache::RenderSvg(
    CachedImageEntry& entry,
    int targetWidth,
    int targetHeight) {
    if (!entry.svgDom || entry.intrinsicSize.width() <= 0.0f || entry.intrinsicSize.height() <= 0.0f) {
        return nullptr;
    }

    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(targetWidth, targetHeight));
    if (!surface) {
        return nullptr;
    }

    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(
        static_cast<float>(targetWidth) / entry.intrinsicSize.width(),
        static_cast<float>(targetHeight) / entry.intrinsicSize.height());
    entry.svgDom->setContainerSize(entry.intrinsicSize);
    entry.svgDom->render(canvas);

    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (image) {
        image = image->makeRasterImage();
    }
    if (!image) {
        return nullptr;
    }

    const uint64_t byteCount = static_cast<uint64_t>(targetWidth) *
        static_cast<uint64_t>(targetHeight) * 4ULL;
    if (byteCount <= kMaxScaledImageBytes) {
        if (scaledImageBytes_ > kMaxScaledImageBytes - static_cast<size_t>(byteCount)) {
            ClearScaledImages();
        }
        entry.scaledImages[MakeScaledImageKey(
            static_cast<float>(targetWidth),
            static_cast<float>(targetHeight))] = image;
        scaledImageBytes_ += static_cast<size_t>(byteCount);
    }
    return image;
}

} // namespace mdviewer
