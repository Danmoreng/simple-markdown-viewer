#include "platform/win/win_app.h"

#include <array>

namespace mdviewer::win {

namespace {

constexpr UINT_PTR kAutoScrollTimerId = 2001;
constexpr UINT_PTR kCopiedFeedbackTimerId = 2002;
constexpr UINT kAutoScrollTimerMs = 16;
constexpr float kAutoScrollDeadZone = 2.0f;
constexpr int kLinkClickSlop = 4;

} // namespace

WinApp::WinApp()
    : hostContext_{
          .controller = controller_,
          .surface = surface_,
          .typefaces = typefaces_,
          .imageCache = imageCache_,
          .fileWatcher = fileWatcher_,
      },
      interactionContext_{
          .host = hostContext_,
          .autoScrollTimerId = kAutoScrollTimerId,
          .copiedFeedbackTimerId = kCopiedFeedbackTimerId,
          .autoScrollTimerMs = kAutoScrollTimerMs,
          .autoScrollDeadZone = kAutoScrollDeadZone,
          .linkClickSlop = kLinkClickSlop,
      } {}

std::wstring WinApp::GetSelectedFontFamily() const {
    const std::string& fontFamilyUtf8 = controller_.GetFontFamilyUtf8();
    if (fontFamilyUtf8.empty()) {
        return {};
    }
    return Utf8ToWide(fontFamilyUtf8);
}

std::string WinApp::WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Length <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        utf8.data(),
        utf8Length,
        nullptr,
        nullptr);
    return utf8;
}

std::wstring WinApp::Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return {};
    }

    std::wstring wide(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        wide.data(),
        wideLength);
    return wide;
}

std::filesystem::path WinApp::GetExecutableConfigPath() {
    std::array<wchar_t, MAX_PATH> buffer = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::filesystem::path(L"mdviewer.ini");
    }

    std::filesystem::path executablePath(std::wstring(buffer.data(), length));
    return executablePath.parent_path() / L"mdviewer.ini";
}

} // namespace mdviewer::win
