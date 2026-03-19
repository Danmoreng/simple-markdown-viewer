#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

#include "layout/document_model.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkImage.h"
#pragma warning(pop)

namespace mdviewer {

class DocumentImageCache {
public:
    void Clear();

    std::pair<float, float> GetImageSize(const std::string& url, const std::filesystem::path& baseDir);
    sk_sp<SkImage> GetImage(
        const std::string& url,
        const std::filesystem::path& baseDir,
        float displayWidth,
        float displayHeight);
    void PreloadDocumentImages(const DocumentModel& doc, const std::filesystem::path& baseDir);

private:
    struct CachedImageEntry {
        sk_sp<SkImage> baseImage;
        std::map<uint64_t, sk_sp<SkImage>> scaledImages;
    };

    static std::filesystem::path ResolveImagePath(const std::string& url, const std::filesystem::path& baseDir);
    static std::string MakeCacheKey(const std::filesystem::path& imagePath);
    static uint64_t MakeScaledImageKey(float width, float height);
    static sk_sp<SkImage> CreateRasterImageFromFile(const std::filesystem::path& imagePath);

    sk_sp<SkImage> GetOrLoadBaseImage(const std::string& url, const std::filesystem::path& baseDir);

    std::unordered_map<std::string, CachedImageEntry> entries_;
};

} // namespace mdviewer
