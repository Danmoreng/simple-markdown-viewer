#include "app/viewer_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <utility>

#include "app/document_loader.h"
#include "render/typography.h"
#include "util/path_utils.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

namespace {

constexpr size_t kMaxRecentFiles = 8;

} // namespace

struct ViewerController::LayoutWorker {
    struct Job {
        uint64_t generation = 0;
        std::shared_ptr<const DocumentModel> model;
        sk_sp<SkFontMgr> fontManager;
        sk_sp<SkTypeface> regular, bold, heading, code;
        float width = 0;
        float baseFontSize = kDefaultBaseFontSize;
        LayoutEngine::ImageSizeProvider imageSizes;
        LayoutOptions options;
    };
    struct Result {
        DocumentLayout layout;
        std::string error;
    };
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> next;
    std::optional<Result> completed;
    std::stop_source cancellation;
    uint64_t generation = 0;
    bool pending = false;
    bool shutdown = false;
    std::thread thread;

    LayoutWorker() : thread([this] { Run(); }) {}
    ~LayoutWorker() {
        {
            std::lock_guard lock(mutex);
            shutdown = true;
            cancellation.request_stop();
            next.reset();
        }
        wake.notify_one();
        thread.join();
    }
    void Run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return shutdown || next.has_value(); });
                if (shutdown) return;
                job = std::move(*next);
                next.reset();
            }
            Result result;
            try {
                const DocumentTypefaceSet fonts{job.fontManager.get(), job.regular.get(),
                    job.bold.get(), job.heading.get(), job.code.get()};
                result.layout = LayoutEngine::ComputeLayout(*job.model, job.width, fonts,
                    job.baseFontSize, job.imageSizes, job.options);
            } catch (const LayoutCancelled&) {
                continue;
            } catch (const std::exception& error) {
                result.error = error.what();
            } catch (...) {
                result.error = "Document layout failed.";
            }
            std::lock_guard lock(mutex);
            if (!shutdown && job.generation == generation && !job.options.stopToken.stop_requested())
                completed = std::move(result);
        }
    }
};

ViewerController::ViewerController() = default;
ViewerController::~ViewerController() = default;

void ViewerController::CancelRelayout() {
    if (!layoutWorker_) return;
    std::lock_guard lock(layoutWorker_->mutex);
    layoutWorker_->cancellation.request_stop();
    ++layoutWorker_->generation;
    layoutWorker_->next.reset();
    layoutWorker_->completed.reset();
    layoutWorker_->pending = false;
}

void ViewerController::InvalidateLayoutModel() {
    CancelRelayout();
    layoutModelSnapshot_.reset();
}

void ViewerController::QueueRelayout(float width, const DocumentTypefaceSet& typefaces,
    LayoutEngine::ImageSizeProvider imageSizeProvider, LayoutOptions options) {
    if (appState_.currentFilePath.empty()) return;
    if (!layoutModelSnapshot_) layoutModelSnapshot_ = std::make_shared<DocumentModel>(appState_.docModel);
    std::unordered_map<std::string, std::pair<float, float>> sizes;
    std::unordered_map<std::string, ImageLoadInfo> imageInfo;
    std::vector<const Block*> blocks;
    for (const auto& block : layoutModelSnapshot_->blocks) blocks.push_back(&block);
    while (!blocks.empty()) {
        const Block& block = *blocks.back();
        blocks.pop_back();
        for (const auto& run : block.inlineRuns) {
            if (run.kind != InlineKind::Image || sizes.contains(run.imageSource)) continue;
            sizes[run.imageSource] = imageSizeProvider ? imageSizeProvider(run.imageSource) : std::pair{0.0f, 0.0f};
            if (options.imageLoadInfoProvider) imageInfo[run.imageSource] = options.imageLoadInfoProvider(run.imageSource);
        }
        for (const auto& child : block.children) blocks.push_back(&child);
    }
    if (options.imageLoadInfoProvider) {
        options.imageLoadInfoProvider = [info = std::move(imageInfo)](const std::string& source) {
            const auto found = info.find(source);
            return found == info.end() ? ImageLoadInfo{} : found->second;
        };
    }
    LayoutWorker::Job job;
    job.model = layoutModelSnapshot_;
    job.fontManager = sk_ref_sp(typefaces.fontMgr);
    job.regular = sk_ref_sp(typefaces.regular);
    job.bold = sk_ref_sp(typefaces.bold);
    job.heading = sk_ref_sp(typefaces.heading);
    job.code = sk_ref_sp(typefaces.code);
    job.width = width;
    job.baseFontSize = ClampBaseFontSize(appState_.baseFontSize);
    job.imageSizes = [sizes = std::move(sizes)](const std::string& source) {
        const auto found = sizes.find(source);
        return found == sizes.end() ? std::pair{0.0f, 0.0f} : found->second;
    };
    job.options = std::move(options);
    if (!layoutWorker_) layoutWorker_ = std::make_unique<LayoutWorker>();
    {
        std::lock_guard lock(layoutWorker_->mutex);
        layoutWorker_->cancellation.request_stop();
        layoutWorker_->cancellation = std::stop_source{};
        job.options.stopToken = layoutWorker_->cancellation.get_token();
        job.generation = ++layoutWorker_->generation;
        layoutWorker_->next = std::move(job);
        layoutWorker_->completed.reset();
        layoutWorker_->pending = true;
    }
    lastLayoutError_.clear();
    layoutWorker_->wake.notify_one();
}

bool ViewerController::PollRelayout() {
    if (!layoutWorker_) return false;
    std::optional<LayoutWorker::Result> result;
    {
        std::lock_guard lock(layoutWorker_->mutex);
        if (!layoutWorker_->completed) return false;
        result = std::move(layoutWorker_->completed);
        layoutWorker_->completed.reset();
        layoutWorker_->pending = false;
    }
    lastLayoutError_ = std::move(result->error);
    if (!lastLayoutError_.empty()) return false;
    std::lock_guard lock(appState_.mtx);
    appState_.ReplaceDocumentLayout(std::move(result->layout));
    appState_.codeBlockButtons.clear();
    appState_.imageLoadButtons.clear();
    appState_.horizontalScrollbars.clear();
    appState_.needsRepaint = true;
    return true;
}

bool ViewerController::IsRelayoutPending() const {
    if (!layoutWorker_) return false;
    std::lock_guard lock(layoutWorker_->mutex);
    return layoutWorker_->pending;
}

void ViewerController::SetConfigPath(std::filesystem::path path) {
    configPath_ = std::move(path);
}

void ViewerController::SetLegacyConfigPath(std::filesystem::path path) {
    legacyConfigPath_ = std::move(path);
}

bool ViewerController::LoadConfig() {
    appState_.theme = ThemeMode::Light;
    appState_.outlineSide = OutlineSide::Left;
    appState_.outlineWidth = kDefaultOutlineWidth;
    appState_.baseFontSize = kDefaultBaseFontSize;
    fontFamilyUtf8_.clear();
    windowPlacement_.reset();
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
    appState_.outlineWidth = ClampOutlineWidth(config->outlineWidth);
    appState_.baseFontSize = ClampBaseFontSize(config->baseFontSize);
    fontFamilyUtf8_ = config->fontFamilyUtf8;
    windowPlacement_ = config->windowPlacement;
    for (const auto& recentFile : config->recentFiles) {
        if (!recentFile.pathUtf8.empty()) {
            const std::u8string recentPathUtf8(recentFile.pathUtf8.begin(), recentFile.pathUtf8.end());
            AppendRecentFileFromConfig(
                std::filesystem::path(recentPathUtf8),
                recentFile.openedAtUnixSeconds,
                recentFile.scrollOffset);
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
    config.outlineWidth = ClampOutlineWidth(appState_.outlineWidth);
    config.fontFamilyUtf8 = fontFamilyUtf8_;
    config.baseFontSize = appState_.baseFontSize;
    config.windowPlacement = windowPlacement_;
    config.recentFiles.reserve(recentFiles_.size());
    const std::filesystem::path currentPath = appState_.currentFilePath.empty()
        ? std::filesystem::path{}
        : NormalizeRecentFilePath(appState_.currentFilePath);
    for (const auto& recentFile : recentFiles_) {
        const auto recentFileUtf8 = recentFile.path.u8string();
        RecentFileConfigEntry entry;
        entry.pathUtf8.assign(recentFileUtf8.begin(), recentFileUtf8.end());
        entry.openedAtUnixSeconds = recentFile.openedAtUnixSeconds;
        entry.scrollOffset = !currentPath.empty() && recentFile.path == currentPath
            ? std::max(appState_.scrollOffset, 0.0f)
            : recentFile.scrollOffset;
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
    appState_.outlineToggleHovered = false;
    appState_.needsRepaint = true;
    return true;
}

bool ViewerController::ToggleOutlineCollapsed() {
    std::lock_guard<std::mutex> lock(appState_.mtx);
    appState_.outlineCollapsed = !appState_.outlineCollapsed;
    appState_.outlineToggleHovered = false;
    appState_.outlineFocused = true;
    appState_.outlineLastDocumentScrollOffset = -1.0f;
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
    const DocumentTypefaceSet& typefaces,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider,
    bool pushHistory,
    bool updateRecentFiles,
    LayoutOptions options) {
    InvalidateLayoutModel();
    const auto documentPath = NormalizeDocumentPath(path);
    auto result = LoadDocumentFromPath(documentPath);
    if (result.status == DocumentLoadStatus::BinaryFile) {
        return OpenDocumentStatus::BinaryFile;
    }
    if (result.status == DocumentLoadStatus::FileReadError) {
        return OpenDocumentStatus::FileReadError;
    }
    if (result.status == DocumentLoadStatus::FileTooLarge) {
        return OpenDocumentStatus::FileTooLarge;
    }

    if (preloadDocument) {
        preloadDocument(result.docModel, documentPath.parent_path());
    }

    auto layout = LayoutEngine::ComputeLayout(
        result.docModel,
        width,
        typefaces,
        ClampBaseFontSize(appState_.baseFontSize),
        imageSizeProvider, options);

    RememberCurrentScrollPosition();
    const float rememberedScrollOffset = GetRememberedScrollOffset(documentPath);
    appState_.SetFile(documentPath, std::move(result.sourceText), std::move(result.docModel), std::move(layout), pushHistory);
    appState_.scrollOffset = rememberedScrollOffset;
    appState_.currentFileLastWriteTime = TryGetFileWriteTime(documentPath);
    if (updateRecentFiles) {
        AddRecentFile(documentPath, GetCurrentUnixSeconds());
    }
    return OpenDocumentStatus::Success;
}

OpenDocumentStatus ViewerController::ReloadCurrentFile(
    float width,
    const DocumentTypefaceSet& typefaces,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider,
    LayoutOptions options) {
    const std::filesystem::path currentPath = appState_.currentFilePath;
    if (currentPath.empty()) {
        return OpenDocumentStatus::FileReadError;
    }

    return OpenFile(currentPath, width, typefaces, preloadDocument, imageSizeProvider, false, false, options);
}

bool ViewerController::Relayout(
    float width,
    const DocumentTypefaceSet& typefaces,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider,
    LayoutOptions options) {
    CancelRelayout();
    if (appState_.sourceText.empty()) return false;
    const auto& docModel = appState_.docModel;
    const auto& currentPath = appState_.currentFilePath;

    if (preloadDocument) {
        preloadDocument(docModel, currentPath.parent_path());
    }

    auto layout = LayoutEngine::ComputeLayout(
        docModel,
        width,
        typefaces,
        ClampBaseFontSize(appState_.baseFontSize),
        imageSizeProvider, options);

    {
        std::lock_guard<std::mutex> lock(appState_.mtx);
        appState_.ReplaceDocumentLayout(std::move(layout));
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
    return mdviewer::ResolveLinkTarget(appState_.currentFilePath, url, forceExternal, true);
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
    float scrollOffset = 0.0f;
    const auto existing = std::find_if(
        recentFiles_.begin(),
        recentFiles_.end(),
        [&](const RecentFileEntry& entry) {
            return entry.path == normalizedPath;
        });
    if (existing != recentFiles_.end()) {
        scrollOffset = existing->scrollOffset;
    }
    recentFiles_.erase(
        std::remove_if(
            recentFiles_.begin(),
            recentFiles_.end(),
            [&](const RecentFileEntry& entry) {
                return entry.path == normalizedPath;
            }),
        recentFiles_.end());
    recentFiles_.insert(recentFiles_.begin(), RecentFileEntry{normalizedPath, openedAtUnixSeconds, scrollOffset});
    if (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.resize(kMaxRecentFiles);
    }
}

float ViewerController::GetRememberedScrollOffset(const std::filesystem::path& path) const {
    const std::filesystem::path normalizedPath = NormalizeRecentFilePath(path);
    const auto existing = std::find_if(
        recentFiles_.begin(),
        recentFiles_.end(),
        [&](const RecentFileEntry& entry) {
            return entry.path == normalizedPath;
        });
    return existing == recentFiles_.end() ? 0.0f : std::max(existing->scrollOffset, 0.0f);
}

void ViewerController::RememberCurrentScrollPosition() {
    if (appState_.currentFilePath.empty()) {
        return;
    }

    const std::filesystem::path normalizedPath = NormalizeRecentFilePath(appState_.currentFilePath);
    const auto existing = std::find_if(
        recentFiles_.begin(),
        recentFiles_.end(),
        [&](const RecentFileEntry& entry) {
            return entry.path == normalizedPath;
        });
    if (existing != recentFiles_.end()) {
        existing->scrollOffset = std::max(appState_.scrollOffset, 0.0f);
    }
}

void ViewerController::AppendRecentFileFromConfig(
    const std::filesystem::path& path,
    long long openedAtUnixSeconds,
    float scrollOffset) {
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

    recentFiles_.push_back(RecentFileEntry{
        normalizedPath,
        openedAtUnixSeconds,
        std::isfinite(scrollOffset) ? std::max(scrollOffset, 0.0f) : 0.0f,
    });
    if (recentFiles_.size() > kMaxRecentFiles) {
        recentFiles_.resize(kMaxRecentFiles);
    }
}

} // namespace mdviewer
