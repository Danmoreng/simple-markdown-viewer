#include "app/image_source.h"
#include "app/native_badge.h"
#include "render/image_cache.h"
#include "markdown/markdown_parser.h"
#include "util/path_utils.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace mdviewer::tests {
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> SmallBitmap() {
    // 2x2, 24-bit Windows bitmap with two padded scanlines.
    std::vector<uint8_t> data(70, 0);
    data[0] = 'B'; data[1] = 'M'; data[2] = 70; data[10] = 54;
    data[14] = 40; data[18] = 2; data[22] = 2; data[26] = 1; data[28] = 24;
    data[34] = 16;
    for (size_t i = 54; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 3);
    return data;
}

void WaitForLoads(DocumentImageCache& cache) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (cache.HasPendingLoads() && std::chrono::steady_clock::now() < deadline) {
        cache.PollLoads();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(!cache.HasPendingLoads(), "image test worker did not complete");
}

struct ScratchImages {
    std::filesystem::path path;
    ScratchImages() {
        path = std::filesystem::temp_directory_path() /
            ("mdviewer-image-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~ScratchImages() { std::error_code error; std::filesystem::remove_all(path, error); }
    void Write(const std::filesystem::path& name, const std::vector<uint8_t>& bytes) const {
        std::ofstream stream(path / name, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};
} // namespace

void RunImageCacheTests() {
    {
        int requests = 0;
        DocumentImageCache badges([&](const ImageSource&, const std::atomic_bool&) {
            ++requests; return ImageDownloadResult{};
        });
        const std::string url = "https://img.shields.io/badge/status-local--only-green";
        const auto parsed = ParseNativeBadge(url);
        Check(parsed && parsed->label == "status" && parsed->message == "local-only", "static badge escaped dash");
        Check(ParseNativeBadge("https://img.shields.io/badge/a__b-hello_world-09f?style=flat-square")->label == "a_b", "badge escaped underscore");
        for (const auto* invalid : {"https://img.shields.io/github/actions/workflow/status/a/b/build.yml",
            "https://img.shields.io/badge/a-b-blue?logo=github", "https://img.shields.io.evil/badge/a-b-blue",
            "https://img.shields.io/badge/a-%00-blue", "https://img.shields.io/badge/a-b-unknown"})
            Check(!ParseNativeBadge(invalid), "unsupported badge remains external");
        Check(!badges.GetSourceInfo(url, {}).RequiresConsent(), "native badge needs no download consent");
        const auto size = badges.GetImageSize(url, {});
        Check(size.first > 20 && size.second == 20, "native badge compact size");
        Check(badges.GetImage(url, {}, size.first, size.second) != nullptr, "native Skia badge rasterizes");
        Check(!badges.RequestLoad(url, {}) && requests == 0, "native badge never downloads");
    }
    ScratchImages scratch;
    const auto bitmap = SmallBitmap();
    const auto name = PathFromUtf8("日本語+image.bmp");
    scratch.Write(name, bitmap);
    DocumentImageCache local;
    const auto size = local.GetImageSize(PathToUtf8(name), scratch.path);
    Check(size.first == 2 && size.second == 2, "local Unicode/+ image metadata should load");
    Check(local.DecodedImageCount() == 0 && local.CachedBytes() == 0,
        "metadata lookup must not retain decoded pixels or encoded local files");
    Check(!!local.GetImage(PathToUtf8(name), scratch.path, 2, 2), "local raster pixels should decode when requested");
    Check(local.DecodedImageCount() == 1 && local.CachedBytes() == 16, "base pixel bytes must count toward the cache budget");

    std::atomic_uint downloads{0};
    DocumentImageCache remote([&](const ImageSource&, const std::atomic_bool&) {
        downloads.fetch_add(1); return ImageDownloadResult{bitmap, {}};
    });
    const std::string url = "https://example.invalid/full/path/image.bmp?token=visible";
    auto info = remote.GetSourceInfo(url, scratch.path);
    Check(info.state == ImageLoadState::Blocked && info.displayTarget == url, "full remote URL must be visible before consent");
    remote.PreloadDocumentImages(MarkdownParser::Parse("![remote](" + url + ")"), scratch.path);
    Check(remote.GetImageSize(url, scratch.path).first == 0 && !remote.GetImage(url, scratch.path, 2, 2),
        "unapproved remote images must remain placeholders during layout and rendering");
    Check(downloads.load() == 0 && !remote.HasPendingLoads(), "preload/layout/render must never initiate a download");
    Check(remote.RequestLoad(url, scratch.path), "explicit remote load should start one worker");
    Check(!remote.RequestLoad(url, scratch.path), "repeated load requests must not duplicate workers");
    WaitForLoads(remote);
    Check(downloads.load() == 1 && remote.GetSourceInfo(url, scratch.path).state == ImageLoadState::Ready,
        "completion should publish approved image metadata");
    Check(remote.GetImageSize(url, scratch.path).first == 2 && remote.DecodedImageCount() == 0,
        "download completion should inspect metadata without eagerly decoding pixels");
    Check(!!remote.GetImage(url, scratch.path, 2, 2), "approved remote image should render");
    Check(remote.GetSourceInfo("https://example.invalid/other.bmp", scratch.path).state == ImageLoadState::Blocked,
        "approval must be scoped to the requested image");
    remote.Clear();
    Check(remote.GetSourceInfo(url, scratch.path).state == ImageLoadState::Blocked && remote.CachedBytes() == 0,
        "document clear must discard approvals and image bytes");

#ifdef _WIN32
    const std::string unc = "//host.invalid/share/image.bmp";
    Check(DescribeImageSource(unc, scratch.path).info.kind == ImageSourceKind::NetworkPath,
        "UNC paths must require explicit consent");
    Check(DescribeImageSource("relative.bmp", PathFromUtf8("//host.invalid/share/docs")).info.kind == ImageSourceKind::NetworkPath,
        "relative images under a UNC document directory must require consent");
    Check(DescribeImageSource("file://host.invalid/share/image.bmp", scratch.path).info.kind == ImageSourceKind::NetworkPath,
        "file URL authorities must classify as network paths");
    remote.GetImageSize(unc, scratch.path);
    Check(downloads.load() == 1 && !remote.HasPendingLoads(), "UNC metadata lookup must not start I/O");
#endif
    Check(DescribeImageSource("https://user:password@example.invalid/image", scratch.path).info.kind == ImageSourceKind::Invalid,
        "credential-bearing HTTP URLs must be rejected");
    Check(DescribeImageSource("image%00.bmp", scratch.path).info.kind == ImageSourceKind::Invalid,
        "encoded NUL in an image path must be rejected");

    const std::string mountInfo =
        "1 0 8:1 / / rw - ext4 /dev/sda1 rw\n"
        "2 1 0:2 / /mnt/shared rw - cifs //alice:secret@server/share rw\n"
        "3 1 0:3 / /mnt/nfs\\040docs rw - nfs4 server:/export rw\n"
        "4 1 0:4 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
        "5 2 8:1 / /mnt/shared/local rw - ext4 /dev/sda1 rw\n";
    const auto smbMount = NetworkImageMountDescription("/mnt/shared/image.png", mountInfo);
    Check(smbMount.find("//server/share") != std::string::npos && smbMount.find("alice") == std::string::npos &&
        smbMount.find("secret") == std::string::npos, "SMB mount description must show the server without mount credentials");
    Check(!NetworkImageMountDescription("/mnt/nfs docs/image.png", mountInfo).empty(),
        "mountinfo escaping and NFS mounts should be recognized");
    Check(!NetworkImageMountDescription("/run/user/1000/gvfs/smb-share/image.png", mountInfo).empty(),
        "GVFS network mounts should require consent");
    Check(NetworkImageMountDescription("/mnt/shared/local/image.png", mountInfo).empty() &&
        NetworkImageMountDescription("/mnt/shared-other/image.png", mountInfo).empty(),
        "mount matching must respect path boundaries and more specific local mounts");

    std::filesystem::create_directories(scratch.path / "real");
    scratch.Write(std::filesystem::path("real") / "image.bmp", bitmap);
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(scratch.path / "real", scratch.path / "linked", symlinkError);
#ifndef _WIN32
    Check(!symlinkError, "Linux image safety test must be able to create a local symlink");
#endif
    if (!symlinkError) {
        DocumentImageCache links;
        Check(links.GetImageSize("linked/image.bmp", scratch.path).first == 0,
            "image metadata reads must reject symlink ancestors before following them");
        Check(links.GetSourceInfo("linked/image.bmp", scratch.path).state == ImageLoadState::Failed,
            "a rejected symlink path must report a readable error");
    }

    DocumentImageCache failed([](const ImageSource&, const std::atomic_bool&) {
        return ImageDownloadResult{{}, "Redirect blocked. Destination: https://other.invalid/image"};
    });
    Check(failed.RequestLoad(url, scratch.path), "failure fixture should start");
    WaitForLoads(failed);
    Check(failed.GetSourceInfo(url, scratch.path).state == ImageLoadState::Failed &&
        failed.GetSourceInfo(url, scratch.path).error.find("https://other.invalid/image") != std::string::npos,
        "redirect failures must preserve the destination for inspection");

    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    DocumentImageCache cancellable([cancelled](const ImageSource&, const std::atomic_bool& stop) {
        while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cancelled->store(true); return ImageDownloadResult{{}, "cancelled"};
    });
    Check(cancellable.RequestLoad(url, scratch.path), "cancellation fixture should start");
    const auto clearStart = std::chrono::steady_clock::now();
    cancellable.Clear();
    Check(std::chrono::steady_clock::now() - clearStart < std::chrono::seconds(1), "clear must not block waiting for network workers");
    const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!cancelled->load() && std::chrono::steady_clock::now() < cancelDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(cancelled->load(), "clearing a document must signal worker cancellation");

    // Three distinct 64 MiB SVG rasters exceed the total budget. The least
    // recently used pixels must be evicted; sizing never creates a retained DOM.
    const std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4096\" height=\"4096\"><rect width=\"4096\" height=\"4096\" fill=\"red\"/></svg>";
    const std::vector<uint8_t> svgBytes(svg.begin(), svg.end());
    DocumentImageCache bounded;
    for (int i = 0; i < 3; ++i) {
        const auto svgName = "image" + std::to_string(i) + ".svg";
        scratch.Write(svgName, svgBytes);
        Check(bounded.GetImageSize(svgName, scratch.path).first == 4096, "SVG metadata should expose intrinsic size");
        Check(!!bounded.GetImage(svgName, scratch.path, 4096, 4096), "SVG should render within total cache budget");
        Check(bounded.CachedBytes() <= kImageCacheByteBudget, "original/scaled/encoded image cache must respect the total byte budget");
    }
    Check(bounded.DecodedImageCount() <= 2, "LRU must evict old image rasters instead of retaining all document images");
}
} // namespace mdviewer::tests
