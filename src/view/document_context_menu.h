#pragma once

#include <string>
#include <vector>

#include "app/app_state.h"
#include "view/document_interaction.h"

namespace mdviewer {

enum class DocumentContextCommand {
    CopySelection,
    OpenLink,
    CopyLink
};

struct DocumentContextMenuItem {
    DocumentContextCommand command;
    std::string label;
    bool enabled = true;
};

struct DocumentContextMenu {
    std::vector<DocumentContextMenuItem> items;
    std::string linkUrl;
};

DocumentContextMenu BuildDocumentContextMenu(const AppState& appState, const InteractionTextHit& hit);

} // namespace mdviewer
