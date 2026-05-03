#include "platform/linux/linux_menu.h"

namespace mdviewer::linux_platform {

const std::vector<MenuBarItem>& GetLinuxMenuBarItems() {
    static const std::vector<MenuBarItem> items = {
        {"File", 0},
        {"View", 1},
        {"Theme", 2},
    };
    return items;
}

std::vector<DropdownItem> GetLinuxDropdownItems(const MenuDropdown& menu) {
    std::vector<DropdownItem> items;
    items.reserve(menu.items.size());
    for (const auto& item : menu.items) {
        items.push_back({item.label, item.isSeparator});
    }
    return items;
}

std::vector<MenuDropdown> GetLinuxMenus() {
    return {
        {
            "File",
            {
                {"Open...", MenuCommand::OpenFile},
                {"", MenuCommand::None, true},
                {"Exit", MenuCommand::Exit}
            }
        },
        {
            "View",
            {
                {"Select Font...", MenuCommand::SelectFont},
                {"Use Default Font", MenuCommand::UseDefaultFont},
                {"", MenuCommand::None, true},
                {"Find...", MenuCommand::Find},
                {"", MenuCommand::None, true},
                {"Show Outline", MenuCommand::ToggleOutline},
                {"Outline on Left", MenuCommand::OutlineLeft},
                {"Outline on Right", MenuCommand::OutlineRight}
            }
        },
        {
            "Theme",
            {
                {"Light", MenuCommand::ThemeLight},
                {"Sepia", MenuCommand::ThemeSepia},
                {"Dark", MenuCommand::ThemeDark}
            }
        }
    };
}

} // namespace mdviewer::linux_platform
