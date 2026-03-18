#pragma once
#include <string>
#include <filesystem>
#include <mutex>

#include "layout/document_model.h"
#include "layout/layout_engine.h"

namespace mdviewer {

enum class ThemeMode {
    Light,
    Sepia,
    Dark
};

struct AppState {
    std::filesystem::path currentFilePath;
    std::string sourceText;
    DocumentModel docModel;
    DocumentLayout docLayout;
    ThemeMode theme = ThemeMode::Light;
    float scrollOffset = 0.0f;
    bool isSelecting = false;
    bool isDraggingScrollbar = false;
    bool isAutoScrolling = false;
    float scrollbarDragOffset = 0.0f;
    float autoScrollOriginX = 0.0f;
    float autoScrollOriginY = 0.0f;
    float autoScrollCursorX = 0.0f;
    float autoScrollCursorY = 0.0f;
    size_t selectionAnchor = 0;
    size_t selectionFocus = 0;
    bool needsRepaint = true;
    std::string hoveredUrl;
    std::mutex mtx;

    std::vector<std::filesystem::path> history;
    size_t historyIndex = 0;

    [[nodiscard]] bool HasSelection() const {
        return selectionAnchor != selectionFocus;
    }

    [[nodiscard]] bool CanGoBack() const {
        return historyIndex > 0;
    }

    [[nodiscard]] bool CanGoForward() const {
        return historyIndex + 1 < history.size();
    }

    void PushHistory(const std::filesystem::path& path) {
        if (!history.empty() && history[historyIndex] == path) {
            return;
        }
        if (historyIndex + 1 < history.size()) {
            history.erase(history.begin() + historyIndex + 1, history.end());
        }
        history.push_back(path);
        historyIndex = history.size() - 1;
    }

    void SetFile(const std::filesystem::path& path, std::string text, DocumentModel doc, DocumentLayout layout, bool pushHistory = true) {
        std::lock_guard<std::mutex> lock(mtx);
        currentFilePath = path;
        if (pushHistory) {
            PushHistory(path);
        }
        sourceText = std::move(text);
        docModel = std::move(doc);
        docLayout = std::move(layout);
        scrollOffset = 0.0f;
        isSelecting = false;
        isDraggingScrollbar = false;
        isAutoScrolling = false;
        scrollbarDragOffset = 0.0f;
        autoScrollOriginX = 0.0f;
        autoScrollOriginY = 0.0f;
        autoScrollCursorX = 0.0f;
        autoScrollCursorY = 0.0f;
        selectionAnchor = 0;
        selectionFocus = 0;
        needsRepaint = true;
    }
};

} // namespace mdviewer
