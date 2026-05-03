#include "view/document_context_menu.h"

namespace mdviewer {

DocumentContextMenu BuildDocumentContextMenu(const AppState& appState, const InteractionTextHit& hit) {
    DocumentContextMenu menu;
    const bool hasSelection = appState.HasSelection();
    const bool hasLink = hit.valid && !hit.url.empty();

    menu.items.push_back({
        .command = DocumentContextCommand::CopySelection,
        .label = "Copy",
        .enabled = hasSelection,
    });

    if (hasLink) {
        menu.linkUrl = hit.url;
        menu.items.push_back({
            .command = DocumentContextCommand::OpenLink,
            .label = "Open Link",
            .enabled = true,
        });
        menu.items.push_back({
            .command = DocumentContextCommand::CopyLink,
            .label = "Copy Link",
            .enabled = true,
        });
    }

    return menu;
}

} // namespace mdviewer
