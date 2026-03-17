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
    std::mutex mtx;

    [[nodiscard]] bool HasSelection() const {
        return selectionAnchor != selectionFocus;
    }

    void SetFile(const std::filesystem::path& path, std::string text, DocumentModel doc, DocumentLayout layout) {
        std::lock_guard<std::mutex> lock(mtx);
        currentFilePath = path;
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
