#include "platform/linux/linux_menu.h"

#include "render/pdf_exporter.h"

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
    std::vector<MenuItem> fileItems = {
        {"Open...", MenuCommand::OpenFile},
    };
#if MDVIEWER_ENABLE_PDF
    fileItems.push_back({"Save as PDF...", MenuCommand::SaveAsPdf});
#endif
    fileItems.push_back({"", MenuCommand::None, true});
    fileItems.push_back({"Exit", MenuCommand::Exit});

    return {
        {
            "File",
            std::move(fileItems)
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
