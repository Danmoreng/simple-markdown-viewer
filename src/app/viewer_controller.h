#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/app_state.h"
#include "app/link_resolver.h"

class SkTypeface;

namespace mdviewer {

enum class OpenDocumentStatus {
    Success,
    FileReadError,
    BinaryFile
};

enum class HistoryDirection {
    Back,
    Forward
};

struct HistoryNavigationTarget {
    std::filesystem::path path;
    size_t index = 0;
};

using DocumentPreloadCallback = std::function<void(const DocumentModel&, const std::filesystem::path&)>;

class ViewerController {
public:
    ViewerController() = default;

    AppState& GetMutableAppState() { return appState_; }
    const AppState& GetAppState() const { return appState_; }

    void SetConfigPath(std::filesystem::path path);
    const std::filesystem::path& GetConfigPath() const { return configPath_; }
    bool LoadConfig();
    bool SaveConfig() const;

    ThemeMode GetTheme() const;
    bool SetTheme(ThemeMode theme);

    float GetBaseFontSize() const;
    bool SetBaseFontSize(float baseFontSize);
    bool CanZoomIn(float delta = 1.0f) const;
    bool CanZoomOut(float delta = 1.0f) const;
    bool ZoomIn(float delta = 1.0f);
    bool ZoomOut(float delta = 1.0f);

    const std::string& GetFontFamilyUtf8() const { return fontFamilyUtf8_; }
    bool HasCustomFontFamily() const { return !fontFamilyUtf8_.empty(); }
    bool SetFontFamilyUtf8(std::string fontFamilyUtf8);
    bool ResetFontFamily();

    const std::vector<std::filesystem::path>& GetRecentFiles() const { return recentFiles_; }

    OpenDocumentStatus OpenFile(
        const std::filesystem::path& path,
        float width,
        SkTypeface* typeface,
        const DocumentPreloadCallback& preloadDocument,
        LayoutEngine::ImageSizeProvider imageSizeProvider,
        bool pushHistory = true);

    bool Relayout(
        float width,
        SkTypeface* typeface,
        const DocumentPreloadCallback& preloadDocument,
        LayoutEngine::ImageSizeProvider imageSizeProvider);

    std::optional<HistoryNavigationTarget> GetHistoryNavigationTarget(HistoryDirection direction) const;
    bool CommitHistoryNavigation(size_t historyIndex);

    LinkTarget ResolveLinkTarget(const std::string& url, bool forceExternal) const;

private:
    static std::filesystem::path NormalizeRecentFilePath(const std::filesystem::path& path);
    void AddRecentFile(const std::filesystem::path& path);

    AppState appState_;
    std::filesystem::path configPath_;
    std::string fontFamilyUtf8_;
    std::vector<std::filesystem::path> recentFiles_;
};

} // namespace mdviewer
