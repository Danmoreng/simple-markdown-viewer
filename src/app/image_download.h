#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mdviewer {

inline constexpr size_t kMaxEncodedImageBytes = 16ULL * 1024ULL * 1024ULL;
inline constexpr size_t kImageCacheByteBudget = 128ULL * 1024ULL * 1024ULL;

struct ImageDownloadResult {
    std::vector<uint8_t> bytes;
    std::string error;
};

// Native implementations verify TLS, disable redirects/authentication/cookies,
// and impose connection, total-time and received-byte limits.
ImageDownloadResult DownloadHttpImage(const std::string& url, const std::atomic_bool& cancelled);

} // namespace mdviewer
