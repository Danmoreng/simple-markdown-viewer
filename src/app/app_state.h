#pragma once
#include <string>
#include <filesystem>
#include <mutex>

namespace mdviewer {

struct AppState {
    std::filesystem::path currentFilePath;
    std::string sourceText;
    bool needsRepaint = true;
    std::mutex mtx;

    void SetFile(const std::filesystem::path& path, std::string text) {
        std::lock_guard<std::mutex> lock(mtx);
        currentFilePath = path;
        sourceText = std::move(text);
        needsRepaint = true;
    }
};

} // namespace mdviewer
