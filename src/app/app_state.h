#pragma once
#include <string>
#include <filesystem>
#include <mutex>

#include "layout/document_model.h"
#include "layout/layout_engine.h"

namespace mdviewer {

struct AppState {
    std::filesystem::path currentFilePath;
    std::string sourceText;
    DocumentModel docModel;
    DocumentLayout docLayout;
    float scrollOffset = 0.0f;
    bool isSelecting = false;
    bool isDraggingScrollbar = false;
    float scrollbarDragOffset = 0.0f;
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
        scrollbarDragOffset = 0.0f;
        selectionAnchor = 0;
        selectionFocus = 0;
        needsRepaint = true;
    }
};

} // namespace mdviewer
