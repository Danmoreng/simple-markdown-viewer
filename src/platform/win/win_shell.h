#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <string>

namespace mdviewer::win {

bool OpenExternalUrl(const std::string& url);
bool OpenPath(const std::filesystem::path& path);

} // namespace mdviewer::win
