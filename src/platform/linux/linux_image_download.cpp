#include "app/image_download.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <string_view>

#include <curl/curl.h>

namespace mdviewer {
namespace {
struct Transfer {
    ImageDownloadResult result;
    const std::atomic_bool& cancelled;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    std::string redirect;
};

size_t Receive(char* data, size_t size, size_t count, void* context) {
    auto& transfer = *static_cast<Transfer*>(context);
    if (transfer.cancelled.load() || std::chrono::steady_clock::now() >= transfer.deadline) return 0;
    if (size != 0 && count > kMaxEncodedImageBytes / size) return 0;
    const size_t bytes = size * count;
    if (bytes > kMaxEncodedImageBytes - transfer.result.bytes.size()) {
        transfer.result.error = "Image download exceeds the 16 MiB limit.";
        return 0;
    }
    try {
        if (transfer.result.bytes.size() + bytes > transfer.result.bytes.capacity()) {
            transfer.result.bytes.reserve(std::min(kMaxEncodedImageBytes,
                std::max(transfer.result.bytes.size() + bytes, transfer.result.bytes.capacity() * 2)));
        }
        transfer.result.bytes.insert(transfer.result.bytes.end(), data, data + bytes);
    } catch (...) {
        transfer.result.error = "Not enough memory for the image download.";
        return 0;
    }
    return bytes;
}

size_t ReceiveHeader(char* data, size_t size, size_t count, void* context) {
    const size_t bytes = size * count;
    auto& transfer = *static_cast<Transfer*>(context);
    std::string_view header(data, bytes);
    constexpr std::string_view prefix = "location:";
    if (header.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), header.begin(),
            [](char a, unsigned char b) { return a == std::tolower(b); })) {
        header.remove_prefix(prefix.size());
        while (!header.empty() && (header.front() == ' ' || header.front() == '\t')) header.remove_prefix(1);
        while (!header.empty() && (header.back() == '\r' || header.back() == '\n')) header.remove_suffix(1);
        try { transfer.redirect.assign(header); }
        catch (...) { return 0; }
    }
    return bytes;
}

int Progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto& transfer = *static_cast<Transfer*>(context);
    return transfer.cancelled.load() || std::chrono::steady_clock::now() >= transfer.deadline;
}
} // namespace

ImageDownloadResult DownloadHttpImage(const std::string& url, const std::atomic_bool& cancelled) {
    static std::once_flag initialized;
    static CURLcode initStatus = CURLE_FAILED_INIT;
    std::call_once(initialized, [] { initStatus = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (initStatus != CURLE_OK) return {{}, "Unable to initialize HTTPS support."};
    CURL* handle = curl_easy_init();
    if (!handle) return {{}, "Unable to initialize the image download."};
    Transfer transfer{{}, cancelled};
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "SimpleMarkdownViewer/1");
    curl_easy_setopt(handle, CURLOPT_PROXY, "");
    curl_easy_setopt(handle, CURLOPT_NETRC, CURL_NETRC_IGNORED);
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_NONE);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(kMaxEncodedImageBytes));
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, Receive);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, ReceiveHeader);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, Progress);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &transfer);
    const CURLcode code = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);
    if (status >= 300 && status < 400) {
        transfer.result.error = "Redirect blocked. Destination: " +
            (transfer.redirect.empty() ? std::string("(not supplied)") : transfer.redirect);
    } else if (code != CURLE_OK && transfer.result.error.empty()) {
        transfer.result.error = cancelled.load() ? "Image download cancelled." : curl_easy_strerror(code);
    } else if ((status < 200 || status >= 300) && transfer.result.error.empty()) {
        transfer.result.error = "Image server returned HTTP " + std::to_string(status) + ".";
    }
    if (!transfer.result.error.empty()) transfer.result.bytes.clear();
    return std::move(transfer.result);
}
} // namespace mdviewer
