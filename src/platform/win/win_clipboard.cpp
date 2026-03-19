#include "platform/win/win_clipboard.h"

namespace mdviewer::win {

bool CopyUtf8TextToClipboard(HWND hwnd, const std::string& utf8Text) {
    if (utf8Text.empty()) {
        return false;
    }

    const int wideLength = MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8Text.c_str(),
        static_cast<int>(utf8Text.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return false;
    }

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLength + 1) * sizeof(wchar_t));
    if (!memory) {
        return false;
    }

    auto* wideText = static_cast<wchar_t*>(GlobalLock(memory));
    if (!wideText) {
        GlobalFree(memory);
        return false;
    }

    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8Text.c_str(),
        static_cast<int>(utf8Text.size()),
        wideText,
        wideLength);
    wideText[wideLength] = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboard(hwnd)) {
        GlobalFree(memory);
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        return false;
    }

    CloseClipboard();
    return true;
}

} // namespace mdviewer::win
