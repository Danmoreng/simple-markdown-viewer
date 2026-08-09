#include "view/document_context_menu.h"

#include "app/link_resolver.h"

namespace mdviewer {
namespace {

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

void AddSeparator(DocumentContextMenu& menu) {
    if (!menu.items.empty() && !menu.items.back().isSeparator) {
        menu.items.push_back({
            .command = DocumentContextCommand::None,
            .label = {},
            .enabled = false,
            .isSeparator = true,
        });
    }
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return !left.empty() && !right.empty() && left.lexically_normal() == right.lexically_normal();
}

} // namespace

DocumentContextMenu BuildDocumentContextMenu(const AppState& appState, const InteractionTextHit& hit) {
    DocumentContextMenu menu;
    const bool hasSelection = appState.HasSelection();
    const bool hasImage = hit.valid && hit.kind == InlineKind::Image && !hit.imageSource.empty();
    const std::string linkTarget = !hit.linkTarget.empty()
        ? hit.linkTarget
        : (!hasImage ? hit.url : std::string{});
    const bool hasLink = hit.valid && !linkTarget.empty();
    const bool hasTable = hit.valid && (!hit.tableTsv.empty() || !hit.tableCsv.empty());
    const bool hasDocument = !appState.currentFilePath.empty();
    menu.documentPath = appState.currentFilePath;

    menu.items.push_back({
        .command = DocumentContextCommand::CopySelection,
        .label = "Copy",
        .enabled = hasSelection,
    });

    if (hasImage) {
        AddSeparator(menu);
        menu.imageSource = hit.imageSource;
        const LinkTarget imageTarget = ResolveLinkTarget(appState.currentFilePath, hit.imageSource, false);
        if (imageTarget.kind == LinkTargetKind::InternalDocument ||
            imageTarget.kind == LinkTargetKind::ExternalPath ||
            imageTarget.kind == LinkTargetKind::MissingLocalPath) {
            menu.localImagePath = imageTarget.path;
            menu.imageCopyText = PathToUtf8(imageTarget.path);
        } else {
            menu.imageCopyText = hit.imageSource;
        }
        menu.items.push_back({
            .command = DocumentContextCommand::OpenImage,
            .label = "Open Image",
            .enabled = imageTarget.kind != LinkTargetKind::Invalid,
        });
        menu.items.push_back({
            .command = DocumentContextCommand::CopyImagePath,
            .label = imageTarget.kind == LinkTargetKind::ExternalUrl ? "Copy Image URL" : "Copy Image Path",
            .enabled = !menu.imageCopyText.empty(),
        });
        if (imageTarget.kind == LinkTargetKind::InternalDocument || imageTarget.kind == LinkTargetKind::ExternalPath) {
            menu.items.push_back({
                .command = DocumentContextCommand::RevealImage,
                .label = "Open Image in File Manager",
                .enabled = true,
            });
        }
    }

    if (hasLink) {
        AddSeparator(menu);
        menu.linkUrl = linkTarget;
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

        const LinkTarget target = ResolveLinkTarget(appState.currentFilePath, linkTarget, false);
        if ((target.kind == LinkTargetKind::InternalDocument || target.kind == LinkTargetKind::ExternalPath) &&
            !SamePath(target.path, menu.localImagePath)) {
            menu.localLinkPath = target.path;
            menu.items.push_back({
                .command = DocumentContextCommand::RevealLinkTarget,
                .label = "Open Link Target in File Manager",
                .enabled = true,
            });
        }
    }

    if (hasTable) {
        AddSeparator(menu);
        menu.tableTsv = hit.tableTsv;
        menu.tableCsv = hit.tableCsv;
        menu.items.push_back({
            .command = DocumentContextCommand::CopyTableTsv,
            .label = "Copy Table as TSV",
            .enabled = !menu.tableTsv.empty(),
        });
        menu.items.push_back({
            .command = DocumentContextCommand::CopyTableCsv,
            .label = "Copy Table as CSV",
            .enabled = !menu.tableCsv.empty(),
        });
    }

    AddSeparator(menu);
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
    if (menu.localLinkPath.empty() && menu.localImagePath.empty()) {
        menu.items.push_back({
            .command = DocumentContextCommand::RevealDocument,
            .label = "Open Document in File Manager",
            .enabled = hasDocument,
        });
    }

    return menu;
}

} // namespace mdviewer
