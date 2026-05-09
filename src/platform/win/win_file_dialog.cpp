#include "platform/win/win_file_dialog.h"

#include <array>

#include <commdlg.h>

namespace mdviewer::win {
namespace {

std::filesystem::path DefaultPdfPath(const std::filesystem::path& currentFilePath) {
    if (currentFilePath.empty()) {
        return std::filesystem::path(L"document.pdf");
    }

    std::filesystem::path pdfPath = currentFilePath;
    pdfPath.replace_extension(L".pdf");
    return pdfPath;
}

std::filesystem::path EnsurePdfExtension(std::filesystem::path path) {
    if (path.extension().empty()) {
        path.replace_extension(L".pdf");
    }
    return path;
}

} // namespace

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

std::optional<std::filesystem::path> ShowSavePdfDialog(HWND hwnd, const std::filesystem::path& currentFilePath) {
    std::array<wchar_t, 32768> fileBuffer = {};
    std::filesystem::path defaultPath = DefaultPdfPath(currentFilePath);
    const std::wstring defaultFilename = defaultPath.filename().wstring();
    lstrcpynW(fileBuffer.data(), defaultFilename.c_str(), static_cast<int>(fileBuffer.size()));

    const std::wstring initialDir = defaultPath.has_parent_path() ? defaultPath.parent_path().wstring() : std::wstring();

    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFilter =
        L"PDF Files (*.pdf)\0*.pdf\0"
        L"All Files (*.*)\0*.*\0";
    dialog.lpstrFile = fileBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    dialog.lpstrDefExt = L"pdf";
    dialog.lpstrTitle = L"Save Markdown as PDF";
    if (!initialDir.empty()) {
        dialog.lpstrInitialDir = initialDir.c_str();
    }

    if (!GetSaveFileNameW(&dialog)) {
        return std::nullopt;
    }

    return EnsurePdfExtension(std::filesystem::path(fileBuffer.data()));
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
