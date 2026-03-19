#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace mdviewer::win {

std::optional<std::filesystem::path> ShowOpenFileDialog(HWND hwnd);
std::optional<std::wstring> ShowFontDialog(HWND hwnd, const std::wstring& currentFontFamily);

} // namespace mdviewer::win
