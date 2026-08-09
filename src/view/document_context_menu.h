#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app/app_state.h"
#include "view/document_interaction.h"

namespace mdviewer {

enum class DocumentContextCommand {
    None,
    CopySelection,
    OpenLink,
    CopyLink,
    RevealLinkTarget,
    ReloadDocument,
    CopyDocumentPath,
    RevealDocument
};

struct DocumentContextMenuItem {
    DocumentContextCommand command;
    std::string label;
    bool enabled = true;
    bool isSeparator = false;
};

struct DocumentContextMenu {
    std::vector<DocumentContextMenuItem> items;
    std::string linkUrl;
    std::filesystem::path localLinkPath;
    std::filesystem::path documentPath;
};

DocumentContextMenu BuildDocumentContextMenu(const AppState& appState, const InteractionTextHit& hit);

} // namespace mdviewer
