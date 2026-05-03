#include "app/viewer_controller.h"

#include <algorithm>
#include <chrono>
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

void ViewerController::SetLegacyConfigPath(std::filesystem::path path) {
    legacyConfigPath_ = std::move(path);
}

bool ViewerController::LoadConfig() {
    appState_.theme = ThemeMode::Light;
    appState_.outlineSide = OutlineSide::Left;
    appState_.baseFontSize = kDefaultBaseFontSize;
    fontFamilyUtf8_.clear();
    recentFiles_.clear();

    if (configPath_.empty()) {
        return false;
    }

    std::filesystem::path loadPath = configPath_;
    if (!legacyConfigPath_.empty()) {
        std::error_code existsError;
        const bool canonicalExists = std::filesystem::exists(configPath_, existsError);
        if (!canonicalExists) {
            loadPath = legacyConfigPath_;
        }
    }

    const auto config = LoadAppConfig(loadPath);
    if (!config) {
        return false;
    }

    appState_.theme = config->theme;
    appState_.outlineSide = config->outlineSide;
    appState_.baseFontSize = ClampBaseFontSize(config->baseFontSize);
    fontFamilyUtf8_ = config->fontFamilyUtf8;
    for (const auto& recentFile : config->recentFiles) {
        if (!recentFile.pathUtf8.empty()) {
            const std::u8string recentPathUtf8(recentFile.pathUtf8.begin(), recentFile.pathUtf8.end());
            AppendRecentFileFromConfig(
                std::filesystem::path(recentPathUtf8),
                recentFile.openedAtUnixSeconds);
        }
    }
    return true;
}

bool ViewerController::SaveConfig() const {
    if (configPath_.empty()) {
        return false;
    }

    const std::filesystem::path parentPath = configPath_.parent_path();
    if (!parentPath.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(parentPath, createError);
        if (createError) {
            return false;
        }
    }

    AppConfig config;
    config.theme = appState_.theme;
    config.outlineSide = appState_.outlineSide;
    config.fontFamilyUtf8 = fontFamilyUtf8_;
    config.baseFontSize = appState_.baseFontSize;
    config.recentFiles.reserve(recentFiles_.size());
    for (const auto& recentFile : recentFiles_) {
        const auto recentFileUtf8 = recentFile.path.u8string();
        RecentFileConfigEntry entry;
        entry.pathUtf8.assign(recentFileUtf8.begin(), recentFileUtf8.end());
        entry.openedAtUnixSeconds = recentFile.openedAtUnixSeconds;
        config.recentFiles.push_back(std::move(entry));
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

OutlineSide ViewerController::GetOutlineSide() const {
    return appState_.outlineSide;
}

bool ViewerController::SetOutlineSide(OutlineSide side) {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    if (appState_.outlineSide == side) {
        return false;
    }

    appState_.outlineSide = side;
    appState_.needsRepaint = true;
    return true;
}

bool ViewerController::ToggleOutlineCollapsed() {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    appState_.outlineCollapsed = !appState_.outlineCollapsed;
    appState_.outlineFocused = true;
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
    bool pushHistory,
    bool updateRecentFiles) {
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
    if (updateRecentFiles) {
        AddRecentFile(path, GetCurrentUnixSeconds());
    }
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

    return OpenFile(currentPath, width, typeface, preloadDocument, imageSizeProvider, false, false);
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

long long ViewerController::GetCurrentUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void ViewerController::AddRecentFile(const std::filesystem::path& path, long long openedAtUnixSeconds) {
    const std::filesystem::path normalizedPath = NormalizeRecentFilePath(path);
    recentFiles_.erase(
        std::remove_if(
            recentFiles_.begin(),
            recentFiles_.end(),
            [&](const RecentFileEntry& entry) {
                return entry.path == normalizedPath;
            }),
        recentFiles_.end());
    recentFiles_.insert(recentFiles_.begin(), RecentFileEntry{normalizedPath, openedAtUnixSeconds});
    if (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.resize(kMaxRecentFiles);
    }
}

void ViewerController::AppendRecentFileFromConfig(const std::filesystem::path& path, long long openedAtUnixSeconds) {
    const std::filesystem::path normalizedPath = NormalizeRecentFilePath(path);
    const auto existing = std::find_if(
        recentFiles_.begin(),
        recentFiles_.end(),
        [&](const RecentFileEntry& entry) {
            return entry.path == normalizedPath;
        });
    if (existing != recentFiles_.end()) {
        return;
    }

    recentFiles_.push_back(RecentFileEntry{normalizedPath, openedAtUnixSeconds});
    if (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.resize(kMaxRecentFiles);
    }
}

} // namespace mdviewer
