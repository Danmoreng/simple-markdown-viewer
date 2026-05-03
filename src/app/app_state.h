#pragma once
#include <string>
#include <filesystem>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "app/app_config.h"
#include "layout/document_model.h"
#include "layout/layout_engine.h"
#include "render/menu_renderer.h"
#include "view/scroll_anchor.h"

namespace mdviewer {

struct SkiaFontSystem;

struct AppState {
    std::filesystem::path currentFilePath;
    std::string sourceText;
    DocumentModel docModel;
    DocumentLayout docLayout;
    ThemeMode theme = ThemeMode::Light;
    OutlineSide outlineSide = OutlineSide::Left;
    float baseFontSize = 17.0f;
    float scrollOffset = 0.0f;
    std::optional<ScrollAnchor> relayoutScrollAnchor;
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
    uint64_t copiedFeedbackTimeout = 0; // Tick count when feedback should expire
    uint64_t zoomFeedbackTimeout = 0;
    float zoomFeedbackFontSize = 17.0f;
    bool outlineCollapsed = false;
    bool outlineFocused = false;
    size_t focusedOutlineIndex = 0;
    bool searchActive = false;
    std::string searchQuery;
    std::vector<std::pair<size_t, size_t>> searchMatches;
    size_t currentSearchMatch = 0;
    SkRect searchCloseButtonRect = SkRect::MakeEmpty();
    std::optional<std::filesystem::file_time_type> currentFileLastWriteTime;
    bool pendingLinkClick = false;
    bool pendingLinkForceExternal = false;
    int pendingLinkPressX = 0;
    int pendingLinkPressY = 0;
    std::string pendingLinkUrl;
    
    MenuBarState menuBarState;
    SkiaFontSystem* fontSystem = nullptr;
    
    // Maps code block button regions to the range of text in that code block
    std::vector<std::pair<SkRect, std::pair<size_t, size_t>>> codeBlockButtons;

    mutable std::mutex mtx;

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
        relayoutScrollAnchor.reset();
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
        hoveredUrl.clear();
        copiedFeedbackTimeout = 0;
        zoomFeedbackTimeout = 0;
        zoomFeedbackFontSize = baseFontSize;
        outlineFocused = false;
        focusedOutlineIndex = 0;
        searchActive = false;
        searchQuery.clear();
        searchMatches.clear();
        currentSearchMatch = 0;
        searchCloseButtonRect = SkRect::MakeEmpty();
        currentFileLastWriteTime.reset();
        pendingLinkClick = false;
        pendingLinkForceExternal = false;
        pendingLinkPressX = 0;
        pendingLinkPressY = 0;
        pendingLinkUrl.clear();
        codeBlockButtons.clear();
    }
};

} // namespace mdviewer
