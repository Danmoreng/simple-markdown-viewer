#pragma once

#include <optional>

#include "view/document_context_menu.h"

namespace mdviewer::linux_platform {

std::optional<DocumentContextCommand> ShowDocumentContextMenu(const DocumentContextMenu& menu);

} // namespace mdviewer::linux_platform
