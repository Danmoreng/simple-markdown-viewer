#include "app/viewer_controller.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

#include "app/document_loader.h"
#include "render/typography.h"

namespace mdviewer {

namespace {

constexpr size_t kMaxRecentFiles = 8;

} // namespace

void ViewerController::SetConfigPath(std::filesystem::path path) {
    configPath_ = std::move(path);
}

bool ViewerController::LoadConfig() {
    appState_.theme = ThemeMode::Light;
    appState_.baseFontSize = kDefaultBaseFontSize;
    fontFamilyUtf8_.clear();
    recentFiles_.clear();

    if (configPath_.empty()) {
        return false;
    }

    const auto config = LoadAppConfig(configPath_);
    if (!config) {
        return false;
    }

    appState_.theme = config->theme;
    appState_.baseFontSize = ClampBaseFontSize(config->baseFontSize);
    fontFamilyUtf8_ = config->fontFamilyUtf8;
    for (const auto& recentFileUtf8 : config->recentFilesUtf8) {
        if (!recentFileUtf8.empty()) {
            const std::u8string recentPathUtf8(recentFileUtf8.begin(), recentFileUtf8.end());
            AddRecentFile(std::filesystem::path(recentPathUtf8));
        }
    }
    return true;
}

bool ViewerController::SaveConfig() const {
    if (configPath_.empty()) {
        return false;
    }

    AppConfig config;
    config.theme = appState_.theme;
    config.fontFamilyUtf8 = fontFamilyUtf8_;
    config.baseFontSize = appState_.baseFontSize;
    config.recentFilesUtf8.reserve(recentFiles_.size());
    for (const auto& recentFile : recentFiles_) {
        const auto recentFileUtf8 = recentFile.u8string();
        config.recentFilesUtf8.emplace_back(recentFileUtf8.begin(), recentFileUtf8.end());
    }

    return SaveAppConfig(configPath_, config);
}

ThemeMode ViewerController::GetTheme() const {
    return appState_.theme;
}

bool ViewerController::SetTheme(ThemeMode theme) {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    if (appState_.theme == theme) {
        return false;
    }

    appState_.theme = theme;
    appState_.needsRepaint = true;
    return true;
}

float ViewerController::GetBaseFontSize() const {
    return appState_.baseFontSize;
}

bool ViewerController::SetBaseFontSize(float baseFontSize) {
    const float clampedFontSize = ClampBaseFontSize(baseFontSize);

    std::lock_guard<std::mutex> lock(appState_.mtx);
    if (std::abs(clampedFontSize - appState_.baseFontSize) < 0.01f) {
        return false;
    }

    appState_.baseFontSize = clampedFontSize;
    return true;
}

bool ViewerController::CanZoomIn(float delta) const {
    return GetBaseFontSize() < ClampBaseFontSize(GetBaseFontSize() + delta);
}

bool ViewerController::CanZoomOut(float delta) const {
    return GetBaseFontSize() > ClampBaseFontSize(GetBaseFontSize() - delta);
}

bool ViewerController::ZoomIn(float delta) {
    return SetBaseFontSize(GetBaseFontSize() + delta);
}

bool ViewerController::ZoomOut(float delta) {
    return SetBaseFontSize(GetBaseFontSize() - delta);
}

bool ViewerController::SetFontFamilyUtf8(std::string fontFamilyUtf8) {
    if (fontFamilyUtf8_ == fontFamilyUtf8) {
        return false;
    }

    fontFamilyUtf8_ = std::move(fontFamilyUtf8);
    return true;
}

bool ViewerController::ResetFontFamily() {
    return SetFontFamilyUtf8({});
}

std::optional<std::filesystem::file_time_type> ViewerController::TryGetFileWriteTime(const std::filesystem::path& path) {
    try {
        if (path.empty() || !std::filesystem::exists(path)) {
            return std::nullopt;
        }
        return std::filesystem::last_write_time(path);
    } catch (...) {
        return std::nullopt;
    }
}

OpenDocumentStatus ViewerController::OpenFile(
    const std::filesystem::path& path,
    float width,
    SkTypeface* typeface,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider,
    bool pushHistory) {
    auto result = LoadDocumentFromPath(path);
    if (result.status == DocumentLoadStatus::BinaryFile) {
        return OpenDocumentStatus::BinaryFile;
    }
    if (result.status == DocumentLoadStatus::FileReadError) {
        return OpenDocumentStatus::FileReadError;
    }

    if (preloadDocument) {
        preloadDocument(result.docModel, path.parent_path());
    }

    auto layout = LayoutEngine::ComputeLayout(
        result.docModel,
        width,
        typeface,
        ClampBaseFontSize(appState_.baseFontSize),
        imageSizeProvider);

    appState_.SetFile(path, std::move(result.sourceText), std::move(result.docModel), std::move(layout), pushHistory);
    appState_.currentFileLastWriteTime = TryGetFileWriteTime(path);
    AddRecentFile(path);
    return OpenDocumentStatus::Success;
}

OpenDocumentStatus ViewerController::ReloadCurrentFile(
    float width,
    SkTypeface* typeface,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider) {
    const std::filesystem::path currentPath = appState_.currentFilePath;
    if (currentPath.empty()) {
        return OpenDocumentStatus::FileReadError;
    }

    return OpenFile(currentPath, width, typeface, preloadDocument, imageSizeProvider, false);
}

bool ViewerController::Relayout(
    float width,
    SkTypeface* typeface,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider) {
    DocumentModel docModel;
    std::filesystem::path currentPath;
    {
        std::lock_guard<std::mutex> lock(appState_.mtx);
        if (appState_.sourceText.empty()) {
            return false;
        }
        docModel = appState_.docModel;
        currentPath = appState_.currentFilePath;
    }

    if (preloadDocument) {
        preloadDocument(docModel, currentPath.parent_path());
    }

    auto layout = LayoutEngine::ComputeLayout(
        docModel,
        width,
        typeface,
        ClampBaseFontSize(appState_.baseFontSize),
        imageSizeProvider);

    {
        std::lock_guard<std::mutex> lock(appState_.mtx);
        appState_.docLayout = std::move(layout);
        appState_.needsRepaint = true;
    }
    return true;
}

bool ViewerController::HasCurrentFile() const {
    return !appState_.currentFilePath.empty();
}

bool ViewerController::HasCurrentFileChanged() const {
    if (!HasCurrentFile()) {
        return false;
    }

    const auto currentWriteTime = TryGetFileWriteTime(appState_.currentFilePath);
    if (!currentWriteTime && !appState_.currentFileLastWriteTime) {
        return false;
    }
    if (!currentWriteTime || !appState_.currentFileLastWriteTime) {
        return true;
    }
    return *currentWriteTime != *appState_.currentFileLastWriteTime;
}

void ViewerController::SyncCurrentFileWriteTime() {
    if (!HasCurrentFile()) {
        appState_.currentFileLastWriteTime.reset();
        return;
    }

    appState_.currentFileLastWriteTime = TryGetFileWriteTime(appState_.currentFilePath);
}

std::optional<HistoryNavigationTarget> ViewerController::GetHistoryNavigationTarget(HistoryDirection direction) const {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    if (direction == HistoryDirection::Back) {
        if (!appState_.CanGoBack()) {
            return std::nullopt;
        }
        return HistoryNavigationTarget{appState_.history[appState_.historyIndex - 1], appState_.historyIndex - 1};
    }

    if (!appState_.CanGoForward()) {
        return std::nullopt;
    }
    return HistoryNavigationTarget{appState_.history[appState_.historyIndex + 1], appState_.historyIndex + 1};
}

bool ViewerController::CommitHistoryNavigation(size_t historyIndex) {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    if (historyIndex >= appState_.history.size()) {
        return false;
    }

    appState_.historyIndex = historyIndex;
    return true;
}

LinkTarget ViewerController::ResolveLinkTarget(const std::string& url, bool forceExternal) const {
    return mdviewer::ResolveLinkTarget(appState_.currentFilePath, url, forceExternal);
}

std::filesystem::path ViewerController::NormalizeRecentFilePath(const std::filesystem::path& path) {
    try {
        return std::filesystem::absolute(path).lexically_normal();
    } catch (...) {
        return path.lexically_normal();
    }
}

void ViewerController::AddRecentFile(const std::filesystem::path& path) {
    const std::filesystem::path normalizedPath = NormalizeRecentFilePath(path);
    recentFiles_.erase(
        std::remove(recentFiles_.begin(), recentFiles_.end(), normalizedPath),
        recentFiles_.end());
    recentFiles_.insert(recentFiles_.begin(), normalizedPath);
    if (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.resize(kMaxRecentFiles);
    }
}

} // namespace mdviewer
