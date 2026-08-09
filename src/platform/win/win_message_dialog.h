#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace mdviewer::win {

void ShowErrorMessage(HWND owner, const std::string& title, const std::string& message);
bool ConfirmWarning(HWND owner, const std::string& title, const std::string& message);

} // namespace mdviewer::win
