#pragma once

#include <string>
#include <vector>
#include <functional>

#include "render/menu_renderer.h"

namespace mdviewer::linux_platform {

enum class MenuCommand {
    None = 0,
    OpenFile = 1001,
    Exit = 1002,
    SelectFont = 1003,
    UseDefaultFont = 1004,
    Find = 1005,
    ThemeLight = 1101,
    ThemeSepia = 1102,
    ThemeDark = 1103
};

struct MenuItem {
    std::string label;
    MenuCommand command = MenuCommand::None;
    bool isSeparator = false;
};

struct MenuDropdown {
    std::string title;
    std::vector<MenuItem> items;
};

std::vector<MenuDropdown> GetLinuxMenus();
const std::vector<MenuBarItem>& GetLinuxMenuBarItems();
std::vector<DropdownItem> GetLinuxDropdownItems(const MenuDropdown& menu);

} // namespace mdviewer::linux_platform
