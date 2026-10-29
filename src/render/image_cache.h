#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "app/image_download.h"
#include "app/image_source.h"
#include "layout/document_model.h"
#pragma warning(push)
#pragma warning(disable: 4244 4267 5030)
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkSize.h"
#pragma warning(pop)

namespace mdviewer {
struct PendingImageLoad;
using ImageDownloadFunction = std::function<ImageDownloadResult(const ImageSource&, const std::atomic_bool&)>;
class DocumentImageCache {
public:
    explicit DocumentImageCache(ImageDownloadFunction downloader = {});
    ~DocumentImageCache();
    DocumentImageCache(const DocumentImageCache&) = delete;
    DocumentImageCache& operator=(const DocumentImageCache&) = delete;
    void Clear();
    void BeginLiveResize();
    void EndLiveResize();
    [[nodiscard]] bool IsLiveResize() const { return liveResize_; }
    [[nodiscard]] ImageLoadInfo GetSourceInfo(const std::string& url, const std::filesystem::path& baseDir) const;
    bool RequestLoad(const std::string& url, const std::filesystem::path& baseDir);
    bool PollLoads();
    [[nodiscard]] bool HasPendingLoads() const;
    std::pair<float, float> GetImageSize(const std::string& url, const std::filesystem::path& baseDir);
    sk_sp<SkImage> GetImage(const std::string& url, const std::filesystem::path& baseDir, float displayWidth, float displayHeight);
    void PreloadDocumentImages(const DocumentModel& doc, const std::filesystem::path& baseDir);
    [[nodiscard]] size_t CachedBytes() const;
    [[nodiscard]] size_t DecodedImageCount() const;
private:
    struct CachedImageEntry {
        ImageSource source;
        SkSize intrinsicSize = SkSize::Make(0.0f, 0.0f);
        bool metadataLoaded = false;
        bool svg = false;
        std::vector<uint8_t> encoded;
        sk_sp<SkImage> baseImage;
        sk_sp<SkImage> scaledImage;
        uint64_t lastUsed = 0;
    };
    CachedImageEntry* GetOrLoadEntry(const ImageSource& source);
    CachedImageEntry& EnsureEntry(const ImageSource& source);
    bool ReserveBytes(size_t bytes, const std::string& protectedKey, bool evictEncoded);
    static size_t EntryBytes(const CachedImageEntry& entry);
    std::unordered_map<std::string, CachedImageEntry> entries_;
    std::vector<std::shared_ptr<PendingImageLoad>> jobs_;
    ImageDownloadFunction downloader_;
    sk_sp<SkFontMgr> svgFontManager_;
    uint64_t clock_ = 0;
    bool liveResize_ = false;
};
} // namespace mdviewer
