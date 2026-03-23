#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <filesystem>
#include <mutex>
#include <thread>

namespace mdviewer::win {

inline constexpr UINT kMessageWatchedFileChanged = WM_APP + 1;

class WinFileWatcher {
public:
    WinFileWatcher() = default;
    ~WinFileWatcher();

    void Start(HWND hwnd);
    void Stop();
    void SetWatchedFile(const std::filesystem::path& path);
    void ClearWatchedFile();

private:
    void ThreadMain();
    static HANDLE CreateDirectoryWatchHandle(const std::filesystem::path& directory);

    std::mutex mutex_;
    std::thread thread_;
    HWND hwnd_ = nullptr;
    HANDLE wakeEvent_ = nullptr;
    std::filesystem::path watchedDirectory_;
    bool stopRequested_ = false;
};

} // namespace mdviewer::win
