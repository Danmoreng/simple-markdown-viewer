#include "platform/win/win_message_dialog.h"

namespace mdviewer::win {
namespace {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (length <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        length);
    return wide;
}

} // namespace

void ShowErrorMessage(HWND owner, const std::string& title, const std::string& message) {
    const std::wstring wideTitle = Utf8ToWide(title);
    const std::wstring wideMessage = Utf8ToWide(message);
    MessageBoxW(owner, wideMessage.c_str(), wideTitle.c_str(), MB_OK | MB_ICONERROR);
}

bool ConfirmWarning(HWND owner, const std::string& title, const std::string& message) {
    const std::wstring wideTitle = Utf8ToWide(title);
    const std::wstring wideMessage = Utf8ToWide(message);
    return MessageBoxW(
        owner,
        wideMessage.c_str(),
        wideTitle.c_str(),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

} // namespace mdviewer::win
