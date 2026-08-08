#pragma once

#include <cstddef>
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
#pragma warning(disable: 5030)
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkSize.h"
#include "modules/svg/include/SkSVGDOM.h"
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
        sk_sp<SkSVGDOM> svgDom;
        SkSize intrinsicSize = SkSize::Make(0.0f, 0.0f);
        std::map<uint64_t, sk_sp<SkImage>> scaledImages;
    };

    static std::filesystem::path ResolveImagePath(const std::string& url, const std::filesystem::path& baseDir);
    static std::string MakeCacheKey(const std::filesystem::path& imagePath);
    static uint64_t MakeScaledImageKey(float width, float height);
    static sk_sp<SkImage> CreateRasterImageFromFile(const std::filesystem::path& imagePath);
    CachedImageEntry* GetOrLoadEntry(const std::filesystem::path& imagePath);
    sk_sp<SkImage> RenderSvg(CachedImageEntry& entry, int targetWidth, int targetHeight);

    void ClearScaledImages();

    std::unordered_map<std::string, CachedImageEntry> entries_;
    sk_sp<SkFontMgr> svgFontManager_;
    size_t scaledImageBytes_ = 0;
};

} // namespace mdviewer
