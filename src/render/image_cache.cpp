#include "render/image_cache.h"
#include "app/native_badge.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <string_view>
#include <thread>
#include "util/skia_font_utils.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#pragma warning(push)
#pragma warning(disable: 4201 4244 4267)
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkData.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#pragma warning(pop)

namespace mdviewer {
struct PendingImageLoad {
    ImageSource source;
    std::filesystem::path baseDir;
    std::atomic_bool cancelled{false};
    std::atomic_bool finished{false};
    ImageDownloadResult result;
};
namespace {
constexpr int kMaxImageDimension = 8192;
constexpr uint64_t kMaxImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxCachedImageEntries = 512;
constexpr size_t kMaxSvgBytes = 8ULL * 1024ULL * 1024ULL;
constexpr unsigned kMaxPendingLoads = 4;
// Includes cancelled jobs from earlier documents, including blocked OS opens.
std::atomic_uint activeLoads{0};
bool ValidSize(SkSize size) {
    return std::isfinite(size.width()) && std::isfinite(size.height()) && size.width() > 0 && size.height() > 0 &&
        size.width() <= kMaxImageDimension && size.height() <= kMaxImageDimension &&
        static_cast<double>(size.width()) * size.height() <= kMaxImagePixels;
}
ImageDownloadResult ReadImageBytes(const std::filesystem::path& path, const std::atomic_bool& cancelled) {
    // Inspect every ancestor without following links. A local-looking filename
    // must never silently cross a junction/symlink to a network share.
#ifdef _WIN32
    auto prefix = path.root_path();
    for (const auto& component : path.relative_path()) {
        prefix /= component;
        const DWORD attributes = GetFileAttributesW(prefix.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return {{}, "Unable to inspect the image path."};
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return {{}, "Images through symbolic links or reparse points are unsupported. Use a direct image path."};
        }
    }
    struct FileHandle {
        HANDLE value = INVALID_HANDLE_VALUE;
        ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    } file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return {{}, "Unable to open the image file."};
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return {{}, "Image is not a regular file, or is a symbolic link/reparse point."};
    }
    const uint64_t size = (static_cast<uint64_t>(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
#else
    struct FileHandle {
        int value = -1;
        ~FileHandle() { if (value >= 0) close(value); }
    } directory, file;
    directory.value = open(path.is_absolute() ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory.value < 0) return {{}, "Unable to inspect the image directory."};
    std::vector<std::filesystem::path> components;
    for (const auto& component : path.relative_path()) components.push_back(component);
    for (size_t i = 0; i < components.size(); ++i) {
        const bool final = i + 1 == components.size();
        const int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (final ? O_NONBLOCK : O_DIRECTORY);
        const int next = openat(directory.value, components[i].c_str(), flags);
        if (next < 0) return {{}, "Unable to open image path without following symbolic links. Use a direct image path."};
        if (final) file.value = next;
        else { close(directory.value); directory.value = next; }
    }
    struct stat information{};
    if (file.value < 0 || fstat(file.value, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size < 0) {
        return {{}, "Image is not a readable regular file."};
    }
    const uint64_t size = static_cast<uint64_t>(information.st_size);
#endif
    if (size > kMaxEncodedImageBytes) return {{}, "Image exceeds the 16 MiB encoded-file limit."};
    ImageDownloadResult result;
    result.bytes.reserve(static_cast<size_t>(size));
    std::array<char, 16 * 1024> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!cancelled.load() && std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        DWORD received = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &received, nullptr)) {
            return {{}, "Unable to read the complete image file."};
        }
        const auto count = static_cast<size_t>(received);
#else
        const auto received = read(file.value, buffer.data(), buffer.size());
        if (received < 0) {
            if (errno == EINTR) continue;
            return {{}, "Unable to read the complete image file."};
        }
        const auto count = static_cast<size_t>(received);
#endif
        if (count > kMaxEncodedImageBytes - result.bytes.size()) return {{}, "Image exceeds the 16 MiB encoded-file limit."};
        if (result.bytes.size() + count > result.bytes.capacity()) {
            result.bytes.reserve(std::min(kMaxEncodedImageBytes,
                std::max(result.bytes.size() + count, result.bytes.capacity() * 2)));
        }
        result.bytes.insert(result.bytes.end(), buffer.data(), buffer.data() + count);
        if (count == 0) return result;
    }
    return {{}, cancelled.load() ? "Image load cancelled." : "Image load timed out."};
}
std::optional<std::string_view> SvgAttribute(std::string_view source, std::string_view name) {
    const auto root = source.find("<svg");
    if (root == std::string_view::npos) return std::nullopt;
    const auto end = source.find('>', root + 4);
    if (end == std::string_view::npos) return std::nullopt;
    size_t position = root + 4;
    while ((position = source.find(name, position)) != std::string_view::npos && position < end) {
        const auto after = position + name.size();
        if (position > 0 && !std::isspace(static_cast<unsigned char>(source[position - 1]))) { position = after; continue; }
        size_t value = after;
        while (value < end && std::isspace(static_cast<unsigned char>(source[value]))) ++value;
        if (value >= end || source[value++] != '=') { position = after; continue; }
        while (value < end && std::isspace(static_cast<unsigned char>(source[value]))) ++value;
        if (value >= end || (source[value] != '\'' && source[value] != '"')) return std::nullopt;
        const char quote = source[value++];
        const auto close = source.find(quote, value);
        if (close == std::string_view::npos || close > end) return std::nullopt;
        return source.substr(value, close - value);
    }
    return std::nullopt;
}
float SvgDimension(std::string_view source, std::string_view name) {
    const auto text = SvgAttribute(source, name);
    if (!text) return 0;
    float value = 0;
    const auto parsed = std::from_chars(text->data(), text->data() + text->size(), value);
    if (parsed.ec != std::errc{}) return 0;
    const std::string_view unit(parsed.ptr, static_cast<size_t>(text->data() + text->size() - parsed.ptr));
    if (unit.empty() || unit == "px") return value;
    if (unit == "pt") return value * (96.0f / 72.0f);
    if (unit == "pc") return value * 16.0f;
    if (unit == "in") return value * 96.0f;
    if (unit == "cm") return value * (96.0f / 2.54f);
    if (unit == "mm") return value * (96.0f / 25.4f);
    return 0;
}
bool ReadMetadata(const std::vector<uint8_t>& bytes, SkSize& size, bool& svg, std::string& error) {
    if (bytes.empty()) { error = "The image is empty."; return false; }
    const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    svg = source.substr(0, std::min<size_t>(source.size(), 4096)).find("<svg") != std::string_view::npos;
    if (svg) {
        if (bytes.size() > kMaxSvgBytes) { error = "SVG exceeds the 8 MiB limit."; return false; }
        size = SkSize::Make(SvgDimension(source, "width"), SvgDimension(source, "height"));
        if (size.width() <= 0 || size.height() <= 0) {
            const auto viewBox = SvgAttribute(source, "viewBox");
            if (viewBox) {
                const char* position = viewBox->data();
                const char* end = position + viewBox->size();
                float values[4]{};
                bool valid = true;
                for (float& value : values) {
                    while (position < end && (std::isspace(static_cast<unsigned char>(*position)) || *position == ',')) ++position;
                    const auto parsed = std::from_chars(position, end, value);
                    if (parsed.ec != std::errc{} || parsed.ptr == position) { valid = false; break; }
                    position = parsed.ptr;
                }
                if (valid) size = SkSize::Make(values[2], values[3]);
            }
        }
    } else {
        auto codec = SkCodec::MakeFromData(SkData::MakeWithoutCopy(bytes.data(), bytes.size()));
        if (!codec) { error = "Unsupported or invalid image data."; return false; }
        size = SkSize::Make(static_cast<float>(codec->getInfo().width()), static_cast<float>(codec->getInfo().height()));
    }
    if (!ValidSize(size)) { error = "Image dimensions exceed the 8192-pixel / 16-megapixel limit or are invalid."; return false; }
    return true;
}
void PreloadBlocks(DocumentImageCache& cache, const std::vector<Block>& blocks, const std::filesystem::path& baseDir) {
    for (const auto& block : blocks) {
        for (const auto& run : block.inlineRuns) if (run.kind == InlineKind::Image) cache.GetImageSize(run.imageSource, baseDir);
        PreloadBlocks(cache, block.children, baseDir);
    }
}
} // namespace

DocumentImageCache::DocumentImageCache(ImageDownloadFunction downloader) : downloader_(std::move(downloader)) {}
DocumentImageCache::~DocumentImageCache() { Clear(); }
void DocumentImageCache::Clear() {
    for (const auto& job : jobs_) job->cancelled.store(true);
    jobs_.clear(); entries_.clear(); liveResize_ = false;
}
void DocumentImageCache::BeginLiveResize() { liveResize_ = true; }
void DocumentImageCache::EndLiveResize() { liveResize_ = false; }
ImageLoadInfo DocumentImageCache::GetSourceInfo(const std::string& url, const std::filesystem::path& baseDir) const {
    const auto source = DescribeImageSource(url, baseDir);
    const auto found = entries_.find(source.cacheKey);
    if (found == entries_.end() || found->second.source.info.kind != source.info.kind ||
        found->second.source.info.displayTarget != source.info.displayTarget) return source.info;
    return found->second.source.info;
}
DocumentImageCache::CachedImageEntry& DocumentImageCache::EnsureEntry(const ImageSource& source) {
    if (auto found = entries_.find(source.cacheKey); found != entries_.end()) {
        if (found->second.source.info.kind != source.info.kind || found->second.source.info.displayTarget != source.info.displayTarget) {
            for (const auto& job : jobs_) if (job->source.cacheKey == source.cacheKey) job->cancelled.store(true);
            found->second = CachedImageEntry{};
            found->second.source = source;
        }
        found->second.lastUsed = ++clock_;
        return found->second;
    }
    if (entries_.size() >= kMaxCachedImageEntries) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.source.info.state == ImageLoadState::Loading) continue;
            if (oldest == entries_.end() || it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        }
        if (oldest != entries_.end()) entries_.erase(oldest);
    }
    CachedImageEntry entry; entry.source = source; entry.lastUsed = ++clock_;
    return entries_.emplace(source.cacheKey, std::move(entry)).first->second;
}
bool DocumentImageCache::RequestLoad(const std::string& url, const std::filesystem::path& baseDir) {
    const auto source = DescribeImageSource(url, baseDir);
    if (!source.info.RequiresConsent()) return false;
    auto& entry = EnsureEntry(source);
    if (entry.source.info.state == ImageLoadState::Loading || entry.source.info.state == ImageLoadState::Ready) return false;
    if (!ReserveBytes(kMaxEncodedImageBytes, source.cacheKey, true)) {
        entry.source.info.state = ImageLoadState::Failed;
        entry.source.info.error = "The image cache cannot reserve space for another download.";
        return false;
    }
    unsigned active = activeLoads.load();
    do {
        if (active >= kMaxPendingLoads) {
            entry.source.info.state = ImageLoadState::Failed;
            entry.source.info.error = "Four image loads are already active. Try again when one finishes.";
            return false;
        }
    } while (!activeLoads.compare_exchange_weak(active, active + 1));
    std::shared_ptr<PendingImageLoad> job;
    try {
        job = std::make_shared<PendingImageLoad>(); job->source = source; job->baseDir = baseDir;
        entry.source.info.state = ImageLoadState::Loading; entry.source.info.error.clear();
        const auto downloader = downloader_;
        jobs_.push_back(job);
        std::thread([job, downloader] {
            try {
                if (downloader) job->result = downloader(job->source, job->cancelled);
                else if (job->source.info.kind == ImageSourceKind::RemoteUrl) job->result = DownloadHttpImage(job->source.info.originalSource, job->cancelled);
                else {
                    const auto current = DescribeImageSource(job->source.info.originalSource, job->baseDir);
                    if (current.cacheKey != job->source.cacheKey || current.info.kind != job->source.info.kind ||
                        current.info.displayTarget != job->source.info.displayTarget) {
                        job->result = {{}, "Image location changed. Inspect the target and load it again."};
                    } else job->result = ReadImageBytes(job->source.resolvedPath, job->cancelled);
                }
            } catch (...) { job->result = {{}, "Image load failed."}; }
            job->finished.store(true, std::memory_order_release);
            activeLoads.fetch_sub(1);
        }).detach();
    } catch (...) {
        activeLoads.fetch_sub(1);
        jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), job), jobs_.end());
        entry.source.info.state = ImageLoadState::Failed; entry.source.info.error = "Unable to start the image download.";
        return false;
    }
    return true;
}
bool DocumentImageCache::HasPendingLoads() const { return !jobs_.empty(); }
bool DocumentImageCache::PollLoads() {
    bool changed = false;
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const auto& job = *it;
        if (!job->finished.load(std::memory_order_acquire)) { ++it; continue; }
        if (!job->cancelled.load()) {
            auto& entry = EnsureEntry(job->source); auto& result = job->result;
            if (result.bytes.size() > kMaxEncodedImageBytes) result.error = "Image exceeds the 16 MiB limit.";
            if (result.error.empty()) ReadMetadata(result.bytes, entry.intrinsicSize, entry.svg, result.error);
            if (result.error.empty() && !ReserveBytes(result.bytes.capacity(), job->source.cacheKey, true)) result.error = "The image cache is full.";
            entry.source.info.error = result.error;
            entry.source.info.state = result.error.empty() ? ImageLoadState::Ready : ImageLoadState::Failed;
            entry.metadataLoaded = true;
            if (result.error.empty()) entry.encoded = std::move(result.bytes);
            changed = true;
        }
        it = jobs_.erase(it);
    }
    return changed;
}
size_t DocumentImageCache::EntryBytes(const CachedImageEntry& entry) {
    size_t bytes = entry.encoded.capacity();
    if (entry.baseImage) bytes += static_cast<size_t>(entry.baseImage->width()) * entry.baseImage->height() * 4;
    if (entry.scaledImage) bytes += static_cast<size_t>(entry.scaledImage->width()) * entry.scaledImage->height() * 4;
    return bytes;
}
size_t DocumentImageCache::CachedBytes() const {
    // Reserve each worker's maximum encoded result while it runs or awaits
    // publication, so background completion cannot overrun the cache budget.
    size_t bytes = jobs_.size() * kMaxEncodedImageBytes;
    for (const auto& [key, entry] : entries_) { (void)key; bytes += EntryBytes(entry); }
    return bytes;
}
size_t DocumentImageCache::DecodedImageCount() const {
    size_t count = 0; for (const auto& [key, entry] : entries_) { (void)key; count += !!entry.baseImage + !!entry.scaledImage; } return count;
}
bool DocumentImageCache::ReserveBytes(size_t bytes, const std::string& protectedKey, bool evictEncoded) {
    if (bytes > kImageCacheByteBudget) return false;
    while (CachedBytes() > kImageCacheByteBudget - bytes) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->first == protectedKey || it->second.source.info.state == ImageLoadState::Loading) continue;
            const auto& entry = it->second;
            if (!entry.baseImage && !entry.scaledImage && (!evictEncoded || entry.encoded.empty())) continue;
            if (oldest == entries_.end() || entry.lastUsed < oldest->second.lastUsed) oldest = it;
        }
        if (oldest == entries_.end()) return false;
        auto& entry = oldest->second;
        if (entry.scaledImage) entry.scaledImage.reset();
        else if (entry.baseImage) entry.baseImage.reset();
        else {
            std::vector<uint8_t>().swap(entry.encoded); entry.metadataLoaded = false;
            entry.source.info.state = ImageLoadState::Blocked;
            entry.source.info.error = "Image data was released from the cache. Load it again to retrieve it.";
        }
    }
    return true;
}
DocumentImageCache::CachedImageEntry* DocumentImageCache::GetOrLoadEntry(const ImageSource& source) {
    if (source.info.kind == ImageSourceKind::Invalid) return nullptr;
    auto& entry = EnsureEntry(source);
    if (source.info.kind == ImageSourceKind::NativeBadge) {
        if (!entry.metadataLoaded) {
            const auto badge = ParseNativeBadge(source.info.originalSource);
            if (!badge) return nullptr;
            if (!svgFontManager_) svgFontManager_ = CreateFontManager();
            SkFont font(CreateDefaultTypeface(svgFontManager_), 11.0f);
            const float left = badge->label.empty() ? 0.0f : std::ceil(font.measureText(badge->label.data(), badge->label.size(), SkTextEncoding::kUTF8)) + 10.0f;
            const float right = std::ceil(font.measureText(badge->message.data(), badge->message.size(), SkTextEncoding::kUTF8)) + 10.0f;
            entry.intrinsicSize = SkSize::Make(left + right, 20.0f);
            entry.metadataLoaded = true;
        }
        return &entry;
    }
    if (source.info.RequiresConsent()) return entry.source.info.state == ImageLoadState::Ready ? &entry : nullptr;
    if (!entry.metadataLoaded) {
        const std::atomic_bool cancelled{false}; auto result = ReadImageBytes(source.resolvedPath, cancelled);
        if (result.error.empty()) ReadMetadata(result.bytes, entry.intrinsicSize, entry.svg, result.error);
        entry.metadataLoaded = true; entry.source.info.error = result.error;
        entry.source.info.state = result.error.empty() ? ImageLoadState::Local : ImageLoadState::Failed;
    }
    return entry.source.info.state == ImageLoadState::Failed ? nullptr : &entry;
}
std::pair<float, float> DocumentImageCache::GetImageSize(const std::string& url, const std::filesystem::path& baseDir) {
    auto* entry = GetOrLoadEntry(DescribeImageSource(url, baseDir));
    return entry ? std::pair{entry->intrinsicSize.width(), entry->intrinsicSize.height()} : std::pair{0.0f, 0.0f};
}
sk_sp<SkImage> DocumentImageCache::GetImage(const std::string& url, const std::filesystem::path& baseDir, float displayWidth, float displayHeight) {
    if (!std::isfinite(displayWidth) || !std::isfinite(displayHeight) || displayWidth <= 0 || displayHeight <= 0) return nullptr;
    const auto source = DescribeImageSource(url, baseDir); auto* entry = GetOrLoadEntry(source);
    if (!entry) return nullptr;
    if (liveResize_) return entry->scaledImage ? entry->scaledImage : entry->baseImage;
    const double scale = std::min({1.0, kMaxImageDimension / static_cast<double>(displayWidth), kMaxImageDimension / static_cast<double>(displayHeight),
        std::sqrt(kMaxImagePixels / (static_cast<double>(displayWidth) * displayHeight))});
    const int width = std::max(1, static_cast<int>(std::floor(displayWidth * scale)));
    const int height = std::max(1, static_cast<int>(std::floor(displayHeight * scale)));
    if (entry->scaledImage && entry->scaledImage->width() == width && entry->scaledImage->height() == height) return entry->scaledImage;
    if (entry->baseImage && entry->baseImage->width() == width && entry->baseImage->height() == height) return entry->baseImage;
    entry->scaledImage.reset();
    if (source.info.kind == ImageSourceKind::NativeBadge) {
        const auto badge = ParseNativeBadge(url);
        if (!badge || !ReserveBytes(static_cast<size_t>(width) * height * 4, source.cacheKey, false)) return nullptr;
        auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
        if (!surface) return nullptr;
        auto* canvas = surface->getCanvas();
        canvas->clear(SK_ColorTRANSPARENT);
        canvas->scale(width / entry->intrinsicSize.width(), height / 20.0f);
        SkFont font(CreateDefaultTypeface(svgFontManager_), 11.0f);
        font.setEdging(SkFont::Edging::kAntiAlias);
        const float left = badge->label.empty() ? 0.0f : std::ceil(font.measureText(badge->label.data(), badge->label.size(), SkTextEncoding::kUTF8)) + 10.0f;
        const float total = entry->intrinsicSize.width();
        canvas->clipRRect(SkRRect::MakeRectXY(SkRect::MakeWH(total, 20), badge->square ? 0.0f : 3.0f, badge->square ? 0.0f : 3.0f), true);
        SkPaint paint;
        paint.setColor(0xff555555);
        canvas->drawRect(SkRect::MakeWH(left, 20), paint);
        paint.setColor(badge->color);
        canvas->drawRect(SkRect::MakeXYWH(left, 0, total - left, 20), paint);
        paint.setAntiAlias(true);
        paint.setColor(SK_ColorWHITE);
        canvas->drawSimpleText(badge->label.data(), badge->label.size(), SkTextEncoding::kUTF8, 5, 14, font, paint);
        const auto color = badge->color;
        const unsigned brightness = 299 * SkColorGetR(color) + 587 * SkColorGetG(color) + 114 * SkColorGetB(color);
        paint.setColor(brightness > 160000 ? 0xff202020 : SK_ColorWHITE);
        canvas->drawSimpleText(badge->message.data(), badge->message.size(), SkTextEncoding::kUTF8, left + 5, 14, font, paint);
        entry->scaledImage = surface->makeImageSnapshot();
        return entry->scaledImage;
    }
    if (!entry->baseImage) {
        ImageDownloadResult local; const auto* encoded = &entry->encoded;
        if (!source.info.RequiresConsent()) { const std::atomic_bool cancelled{false}; local = ReadImageBytes(source.resolvedPath, cancelled); encoded = &local.bytes; }
        if (!local.error.empty() || encoded->empty()) return nullptr;
        const auto data = SkData::MakeWithoutCopy(encoded->data(), encoded->size());
        if (entry->svg) {
            const size_t rasterBytes = static_cast<size_t>(width) * height * 4;
            // DOMs are transient; charge a parsing allowance before creating one.
            if (!ReserveBytes(rasterBytes + encoded->size() * 8, source.cacheKey, false)) return nullptr;
            SkMemoryStream stream(data);
            if (!svgFontManager_) svgFontManager_ = CreateFontManager();
            SkSVGDOM::Builder builder; builder.setFontManager(svgFontManager_); builder.setTextShapingFactory(SkShapers::Primitive::Factory());
            auto dom = builder.make(stream); if (!dom) return nullptr;
            auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height)); if (!surface) return nullptr;
            auto* canvas = surface->getCanvas(); canvas->clear(SK_ColorTRANSPARENT);
            canvas->scale(width / entry->intrinsicSize.width(), height / entry->intrinsicSize.height());
            dom->setContainerSize(entry->intrinsicSize); dom->render(canvas);
            entry->scaledImage = surface->makeImageSnapshot(); return entry->scaledImage;
        }
        auto codec = SkCodec::MakeFromData(data); if (!codec) return nullptr;
        const auto info = SkImageInfo::MakeN32Premul(codec->getInfo().width(), codec->getInfo().height());
        if (!ValidSize(SkSize::Make(static_cast<float>(info.width()), static_cast<float>(info.height()))) ||
            !ReserveBytes(info.computeMinByteSize(), source.cacheKey, false)) return nullptr;
        SkBitmap bitmap;
        if (!bitmap.tryAllocPixels(info) || codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes()) != SkCodec::kSuccess) return nullptr;
        bitmap.setImmutable(); entry->baseImage = SkImages::RasterFromBitmap(bitmap);
    }
    if (!entry->baseImage) return nullptr;
    if (entry->baseImage->width() == width && entry->baseImage->height() == height) return entry->baseImage;
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    if (!ReserveBytes(bytes, source.cacheKey, false)) return entry->baseImage;
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height)); if (!surface) return entry->baseImage;
    surface->getCanvas()->clear(SK_ColorTRANSPARENT);
    surface->getCanvas()->drawImageRect(entry->baseImage, SkRect::MakeWH(static_cast<float>(width), static_cast<float>(height)), SkSamplingOptions(SkFilterMode::kLinear));
    entry->scaledImage = surface->makeImageSnapshot(); return entry->scaledImage;
}
void DocumentImageCache::PreloadDocumentImages(const DocumentModel& doc, const std::filesystem::path& baseDir) { PreloadBlocks(*this, doc.blocks, baseDir); }
} // namespace mdviewer
