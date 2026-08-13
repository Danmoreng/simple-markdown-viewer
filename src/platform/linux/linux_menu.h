#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "app/viewer_controller.h"
#include "render/menu_renderer.h"

namespace mdviewer::linux_platform {

enum class MenuCommand {
    None = 0,
    OpenFile = 1001,
    Exit = 1002,
    SelectFont = 1003,
    UseDefaultFont = 1004,
    Find = 1005,
    ToggleOutline = 1006,
    OutlineLeft = 1007,
    OutlineRight = 1008,
    SaveAsPdf = 1009,
    OpenRecentFile = 1010,
    Print = 1011,
    Reload = 1012,
    ResetZoom = 1013,
    ThemeLight = 1101,
    ThemeSepia = 1102,
    ThemeDark = 1103
};

struct MenuItem {
    std::string label;
    MenuCommand command = MenuCommand::None;
    bool isSeparator = false;
    std::filesystem::path path;
    std::string shortcut;
    bool enabled = true;
    bool checked = false;
    std::vector<MenuItem> children;
};

struct MenuDropdown {
    std::string title;
    std::vector<MenuItem> items;
};

std::vector<MenuDropdown> GetLinuxMenus(const ViewerController& controller);
const std::vector<MenuBarItem>& GetLinuxMenuBarItems();
std::vector<DropdownItem> GetLinuxDropdownItems(const MenuDropdown& menu);
std::vector<DropdownItem> GetLinuxDropdownItems(const std::vector<MenuItem>& items);

} // namespace mdviewer::linux_platform
