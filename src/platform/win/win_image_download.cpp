#include "app/image_download.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <algorithm>
#include <memory>

namespace mdviewer {
namespace {
struct CloseInternet {
    void operator()(void* handle) const { if (handle) WinHttpCloseHandle(handle); }
};
using InternetHandle = std::unique_ptr<void, CloseInternet>;

std::wstring ToWide(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string ToUtf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string QueryHeader(HINTERNET request, DWORD header) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes > 64 * 1024) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return {};
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return ToUtf8(value);
}
} // namespace

ImageDownloadResult DownloadHttpImage(const std::string& url, const std::atomic_bool& cancelled) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const std::wstring wideUrl = ToWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
        parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (wideUrl.empty() || !WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts) ||
        parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0 ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) {
        return {{}, "Invalid HTTP image URL."};
    }
    InternetHandle session(WinHttpOpen(L"SimpleMarkdownViewer/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return {{}, "Unable to initialize the image download."};
    WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 5000);
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    if (!connection) return {{}, "Unable to connect to the image server."};
    std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (const auto fragment = resource.find(L'#'); fragment != std::wstring::npos) resource.erase(fragment);
    if (resource.empty()) resource = L"/";
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    if (!request) return {{}, "Unable to create the image request."};
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD logonPolicy = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(request.get(), WINHTTP_OPTION_AUTOLOGON_POLICY, &logonPolicy, sizeof(logonPolicy))) {
        return {{}, "Unable to disable automatic image redirects/authentication."};
    }
    if (cancelled.load()) return {{}, "Image download cancelled."};
    const auto applyRemainingTimeout = [&] {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (cancelled.load() || remaining <= 0) return false;
        DWORD timeout = static_cast<DWORD>(std::min<int64_t>(remaining, 5000));
        return !!WinHttpSetOption(request.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout)) &&
            !!WinHttpSetOption(request.get(), WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &timeout, sizeof(timeout)) &&
            !!WinHttpSetOption(request.get(), WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    };
    if (!applyRemainingTimeout() ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !applyRemainingTimeout() || !WinHttpReceiveResponse(request.get(), nullptr)) {
        return {{}, "Image request failed (connection, timeout, or TLS certificate verification; Windows error " +
            std::to_string(GetLastError()) + ")."};
    }
    DWORD status = 0;
    DWORD statusBytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes, WINHTTP_NO_HEADER_INDEX)) return {{}, "Invalid image server response."};
    if (status >= 300 && status < 400) {
        auto destination = QueryHeader(request.get(), WINHTTP_QUERY_LOCATION);
        return {{}, "Redirect blocked. Destination: " + (destination.empty() ? std::string("(not supplied)") : destination)};
    }
    if (status < 200 || status >= 300) return {{}, "Image server returned HTTP " + std::to_string(status) + "."};
    ImageDownloadResult result;
    uint8_t buffer[16 * 1024];
    while (!cancelled.load() && std::chrono::steady_clock::now() < deadline) {
        DWORD received = 0;
        if (!applyRemainingTimeout()) return {{}, "Image download cancelled or timed out."};
        if (!WinHttpReadData(request.get(), buffer, sizeof(buffer), &received)) return {{}, "Image download failed or timed out."};
        if (received == 0) return result;
        if (received > kMaxEncodedImageBytes - result.bytes.size()) return {{}, "Image download exceeds the 16 MiB limit."};
        if (result.bytes.size() + received > result.bytes.capacity()) {
            result.bytes.reserve(std::min(kMaxEncodedImageBytes,
                std::max(result.bytes.size() + received, result.bytes.capacity() * 2)));
        }
        result.bytes.insert(result.bytes.end(), buffer, buffer + received);
    }
    return {{}, cancelled.load() ? "Image download cancelled." : "Image download exceeded the 30 second time limit."};
}
} // namespace mdviewer
