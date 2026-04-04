#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>

namespace mdviewer::win {

void InstallCrashHandler(const std::filesystem::path& preferredOutputDirectory);

} // namespace mdviewer::win
