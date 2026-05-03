#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>

#include "view/document_context_menu.h"

namespace mdviewer::win {

std::optional<DocumentContextCommand> ShowDocumentContextMenu(
    HWND hwnd,
    const DocumentContextMenu& menu,
    POINT screenPoint);

} // namespace mdviewer::win
