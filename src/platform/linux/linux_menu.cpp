#include "platform/linux/linux_menu.h"

#include "render/pdf_exporter.h"

#include <algorithm>

namespace mdviewer::linux_platform {

namespace {

std::string PathLabel(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

} // namespace

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

std::vector<MenuDropdown> GetLinuxMenus(const std::vector<RecentFileEntry>& recentFiles) {
    std::vector<MenuItem> fileItems = {
        {"Open...", MenuCommand::OpenFile},
    };
#if MDVIEWER_ENABLE_PDF
    fileItems.push_back({"Save as PDF...", MenuCommand::SaveAsPdf});
#endif
    if (!recentFiles.empty()) {
        fileItems.push_back({"", MenuCommand::None, true});
        const size_t recentCount = std::min(recentFiles.size(), static_cast<size_t>(8));
        for (size_t index = 0; index < recentCount; ++index) {
            fileItems.push_back({
                std::to_string(index + 1) + "  " + PathLabel(recentFiles[index].path),
                MenuCommand::OpenRecentFile,
                false,
                recentFiles[index].path,
            });
        }
    }
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
