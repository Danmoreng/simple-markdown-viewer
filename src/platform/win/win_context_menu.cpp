#include "platform/win/win_context_menu.h"

#include "platform/win/win_app.h"

namespace mdviewer::win {
namespace {

constexpr UINT_PTR kContextCommandBase = 41000;

UINT_PTR CommandIdForIndex(size_t index) {
    return kContextCommandBase + static_cast<UINT_PTR>(index);
}

} // namespace

std::optional<DocumentContextCommand> ShowDocumentContextMenu(
    HWND hwnd,
    const DocumentContextMenu& menu,
    POINT screenPoint) {
    if (menu.items.empty()) {
        return std::nullopt;
    }

    HMENU popup = CreatePopupMenu();
    if (!popup) {
        return std::nullopt;
    }

    for (size_t index = 0; index < menu.items.size(); ++index) {
        const auto& item = menu.items[index];
        const std::wstring label = WinApp::Utf8ToWide(item.label);
        AppendMenuW(
            popup,
            MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED),
            CommandIdForIndex(index),
            label.c_str());
    }

    const UINT selected = TrackPopupMenu(
        popup,
        TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        screenPoint.x,
        screenPoint.y,
        0,
        hwnd,
        nullptr);
    DestroyMenu(popup);

    if (selected < kContextCommandBase) {
        return std::nullopt;
    }

    const size_t index = static_cast<size_t>(selected - kContextCommandBase);
    if (index >= menu.items.size() || !menu.items[index].enabled) {
        return std::nullopt;
    }

    return menu.items[index].command;
}

} // namespace mdviewer::win
