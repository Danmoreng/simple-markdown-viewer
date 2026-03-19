#include "platform/win/win_file_dialog.h"

#include <array>

#include <commdlg.h>

namespace mdviewer::win {

std::optional<std::filesystem::path> ShowOpenFileDialog(HWND hwnd) {
    std::array<wchar_t, MAX_PATH> fileBuffer = {};

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter =
        L"Markdown Files (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.txt\0"
        L"All Files (*.*)\0*.*\0";
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrDefExt = L"md";

    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }

    return std::filesystem::path(fileBuffer.data());
}

std::optional<std::wstring> ShowFontDialog(HWND hwnd, const std::wstring& currentFontFamily) {
    LOGFONTW logFont = {};
    if (!currentFontFamily.empty()) {
        lstrcpynW(logFont.lfFaceName, currentFontFamily.c_str(), LF_FACESIZE);
    }

    CHOOSEFONTW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpLogFont = &logFont;
    dialog.Flags = CF_SCREENFONTS | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOSIZESEL;

    if (!ChooseFontW(&dialog)) {
        return std::nullopt;
    }

    return std::wstring(logFont.lfFaceName);
}

} // namespace mdviewer::win
