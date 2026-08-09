#include "view/document_context_menu.h"

#include "app/link_resolver.h"

namespace mdviewer {

DocumentContextMenu BuildDocumentContextMenu(const AppState& appState, const InteractionTextHit& hit) {
    DocumentContextMenu menu;
    const bool hasSelection = appState.HasSelection();
    const bool hasLink = hit.valid && !hit.url.empty();
    const bool hasDocument = !appState.currentFilePath.empty();
    menu.documentPath = appState.currentFilePath;

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

        const LinkTarget target = ResolveLinkTarget(appState.currentFilePath, hit.url, false);
        if (target.kind == LinkTargetKind::InternalDocument || target.kind == LinkTargetKind::ExternalPath) {
            menu.localLinkPath = target.path;
            menu.items.push_back({
                .command = DocumentContextCommand::RevealLinkTarget,
                .label = "Open Link Target in File Manager",
                .enabled = true,
            });
        }
    }

    menu.items.push_back({
        .command = DocumentContextCommand::None,
        .label = {},
        .enabled = false,
        .isSeparator = true,
    });
    menu.items.push_back({
        .command = DocumentContextCommand::ReloadDocument,
        .label = "Reload",
        .enabled = hasDocument,
    });
    menu.items.push_back({
        .command = DocumentContextCommand::CopyDocumentPath,
        .label = "Copy Document Path",
        .enabled = hasDocument,
    });
    if (menu.localLinkPath.empty()) {
        menu.items.push_back({
            .command = DocumentContextCommand::RevealDocument,
            .label = "Open Document in File Manager",
            .enabled = hasDocument,
        });
    }

    return menu;
}

} // namespace mdviewer
