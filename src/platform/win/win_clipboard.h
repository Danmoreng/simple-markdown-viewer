#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace mdviewer::win {

bool CopyUtf8TextToClipboard(HWND hwnd, const std::string& utf8Text);
std::string GetUtf8TextFromClipboard(HWND hwnd);

} // namespace mdviewer::win
