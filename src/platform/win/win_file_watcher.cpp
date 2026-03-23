#include "platform/win/win_file_watcher.h"

namespace mdviewer::win {

WinFileWatcher::~WinFileWatcher() {
    Stop();
}

void WinFileWatcher::Start(HWND hwnd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_.joinable()) {
        hwnd_ = hwnd;
        return;
    }

    hwnd_ = hwnd;
    stopRequested_ = false;
    wakeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&WinFileWatcher::ThreadMain, this);
}

void WinFileWatcher::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!thread_.joinable()) {
            return;
        }
        stopRequested_ = true;
        if (wakeEvent_) {
            SetEvent(wakeEvent_);
        }
    }

    thread_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    if (wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
    watchedDirectory_.clear();
    hwnd_ = nullptr;
    stopRequested_ = false;
}

void WinFileWatcher::SetWatchedFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    watchedDirectory_ = path.empty() ? std::filesystem::path{} : path.parent_path();
    if (wakeEvent_) {
        SetEvent(wakeEvent_);
    }
}

void WinFileWatcher::ClearWatchedFile() {
    SetWatchedFile({});
}

HANDLE WinFileWatcher::CreateDirectoryWatchHandle(const std::filesystem::path& directory) {
    if (directory.empty()) {
        return nullptr;
    }

    const HANDLE handle = FindFirstChangeNotificationW(
        directory.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);

    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

void WinFileWatcher::ThreadMain() {
    HANDLE changeHandle = nullptr;
    std::filesystem::path activeDirectory;

    auto reconfigure = [&]() -> bool {
        std::filesystem::path desiredDirectory;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_) {
                return false;
            }
            desiredDirectory = watchedDirectory_;
        }

        if (desiredDirectory == activeDirectory) {
            return true;
        }

        if (changeHandle) {
            FindCloseChangeNotification(changeHandle);
            changeHandle = nullptr;
        }

        activeDirectory = desiredDirectory;
        changeHandle = CreateDirectoryWatchHandle(activeDirectory);
        return true;
    };

    if (!reconfigure()) {
        return;
    }

    while (true) {
        HANDLE handles[2] = {wakeEvent_, changeHandle};
        const DWORD handleCount = changeHandle ? 2 : 1;
        const DWORD result = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) {
            if (wakeEvent_) {
                ResetEvent(wakeEvent_);
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopRequested_) {
                    break;
                }
            }

            if (!reconfigure()) {
                break;
            }
            continue;
        }

        if (changeHandle && result == WAIT_OBJECT_0 + 1) {
            HWND hwnd = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                hwnd = hwnd_;
            }
            if (hwnd) {
                PostMessageW(hwnd, kMessageWatchedFileChanged, 0, 0);
            }

            if (!FindNextChangeNotification(changeHandle)) {
                FindCloseChangeNotification(changeHandle);
                changeHandle = CreateDirectoryWatchHandle(activeDirectory);
            }
        }
    }

    if (changeHandle) {
        FindCloseChangeNotification(changeHandle);
    }
}

} // namespace mdviewer::win
