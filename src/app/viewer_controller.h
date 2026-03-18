#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include "app/app_state.h"

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

OpenDocumentStatus OpenDocument(
    AppState& appState,
    const std::filesystem::path& path,
    float width,
    SkTypeface* typeface,
    float baseFontSize,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider,
    bool pushHistory = true);

bool RelayoutDocument(
    AppState& appState,
    float width,
    SkTypeface* typeface,
    float baseFontSize,
    const DocumentPreloadCallback& preloadDocument,
    LayoutEngine::ImageSizeProvider imageSizeProvider);

std::optional<HistoryNavigationTarget> GetHistoryNavigationTarget(AppState& appState, HistoryDirection direction);
void CommitHistoryNavigation(AppState& appState, size_t historyIndex);

} // namespace mdviewer
