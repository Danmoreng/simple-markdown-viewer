#include "app/viewer_controller.h"

#include "app/document_loader.h"
#include "render/typography.h"

namespace mdviewer {

OpenDocumentStatus OpenDocument(
    AppState& appState,
    const std::filesystem::path& path,
    float width,
    SkTypeface* typeface,
    float baseFontSize,
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
        ClampBaseFontSize(baseFontSize),
        imageSizeProvider);

    appState.SetFile(path, std::move(result.sourceText), std::move(result.docModel), std::move(layout), pushHistory);
    return OpenDocumentStatus::Success;
}

bool RelayoutDocument(
    AppState& appState,
    float width,
    SkTypeface* typeface,
    float baseFontSize,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider) {
    DocumentModel docModel;
    std::filesystem::path currentPath;
    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        if (appState.sourceText.empty()) {
            return false;
        }
        docModel = appState.docModel;
        currentPath = appState.currentFilePath;
    }

    if (preloadDocument) {
        preloadDocument(docModel, currentPath.parent_path());
    }

    auto layout = LayoutEngine::ComputeLayout(
        docModel,
        width,
        typeface,
        ClampBaseFontSize(baseFontSize),
        imageSizeProvider);

    {
        std::lock_guard<std::mutex> lock(appState.mtx);
        appState.docLayout = std::move(layout);
        appState.needsRepaint = true;
    }
    return true;
}

std::optional<HistoryNavigationTarget> GetHistoryNavigationTarget(AppState& appState, HistoryDirection direction) {
    std::lock_guard<std::mutex> lock(appState.mtx);
    if (direction == HistoryDirection::Back) {
        if (!appState.CanGoBack()) {
            return std::nullopt;
        }
        return HistoryNavigationTarget{appState.history[appState.historyIndex - 1], appState.historyIndex - 1};
    }

    if (!appState.CanGoForward()) {
        return std::nullopt;
    }
    return HistoryNavigationTarget{appState.history[appState.historyIndex + 1], appState.historyIndex + 1};
}

void CommitHistoryNavigation(AppState& appState, size_t historyIndex) {
    std::lock_guard<std::mutex> lock(appState.mtx);
    appState.historyIndex = historyIndex;
}

} // namespace mdviewer
