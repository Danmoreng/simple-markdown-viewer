#include "platform/linux/linux_menu.h"

#include "render/pdf_exporter.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace mdviewer::linux_platform {

namespace {

std::string PathLabel(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::string FormatOpenedAtLocal(long long unixSeconds) {
    if (unixSeconds <= 0) {
        return {};
    }

    const std::time_t openedAt = static_cast<std::time_t>(unixSeconds);
    std::tm localTime = {};
    if (!localtime_r(&openedAt, &localTime)) {
        return {};
    }

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M");
    return stream.str();
}

} // namespace

const std::vector<MenuBarItem>& GetLinuxMenuBarItems() {
    static const std::vector<MenuBarItem> items = {
        {"File", 0},
        {"View", 1},
    };
    return items;
}

std::vector<DropdownItem> GetLinuxDropdownItems(const std::vector<MenuItem>& menuItems) {
    std::vector<DropdownItem> items;
    items.reserve(menuItems.size());
    for (const auto& item : menuItems) {
        items.push_back({
            item.label,
            item.shortcut,
            item.isSeparator,
            item.enabled,
            item.checked,
            !item.children.empty(),
        });
    }
    return items;
}

std::vector<DropdownItem> GetLinuxDropdownItems(const MenuDropdown& menu) {
    return GetLinuxDropdownItems(menu.items);
}

std::vector<MenuDropdown> GetLinuxMenus(const ViewerController& controller) {
    const auto& appState = controller.GetAppState();
    const auto& recentFiles = controller.GetRecentFiles();
    const bool hasCurrentFile = controller.HasCurrentFile();

    std::vector<MenuItem> fileItems = {
        {"Open...", MenuCommand::OpenFile, false, {}, "Ctrl+O"},
    };
#if MDVIEWER_ENABLE_PDF
    fileItems.push_back({"Save as PDF...", MenuCommand::SaveAsPdf, false, {}, {}, hasCurrentFile});
#endif
    fileItems.push_back({"Print...", MenuCommand::Print, false, {}, "Ctrl+P", hasCurrentFile});
    if (!recentFiles.empty()) {
        fileItems.push_back({"", MenuCommand::None, true});
        const size_t recentCount = std::min(recentFiles.size(), static_cast<size_t>(8));
        for (size_t index = 0; index < recentCount; ++index) {
            fileItems.push_back({
                std::to_string(index + 1) + "  " + PathLabel(recentFiles[index].path),
                MenuCommand::OpenRecentFile,
                false,
                recentFiles[index].path,
                FormatOpenedAtLocal(recentFiles[index].openedAtUnixSeconds),
            });
        }
    }
    fileItems.push_back({"", MenuCommand::None, true});
    fileItems.push_back({"Exit", MenuCommand::Exit});

    std::vector<MenuItem> themeItems = {
        {"Light", MenuCommand::ThemeLight, false, {}, {}, true, appState.theme == ThemeMode::Light},
        {"Sepia", MenuCommand::ThemeSepia, false, {}, {}, true, appState.theme == ThemeMode::Sepia},
        {"Dark", MenuCommand::ThemeDark, false, {}, {}, true, appState.theme == ThemeMode::Dark},
    };

    std::vector<MenuItem> viewItems = {
        {"Select Font...", MenuCommand::SelectFont},
        {"Use Default Font", MenuCommand::UseDefaultFont, false, {}, {}, controller.HasCustomFontFamily()},
        {"", MenuCommand::None, true},
        {"Find...", MenuCommand::Find, false, {}, "Ctrl+F"},
        {"", MenuCommand::None, true},
        {"Show Outline", MenuCommand::ToggleOutline, false, {}, "Ctrl+Shift+O", true, !appState.outlineCollapsed},
        {"Outline on Left", MenuCommand::OutlineLeft, false, {}, {}, true, appState.outlineSide == OutlineSide::Left},
        {"Outline on Right", MenuCommand::OutlineRight, false, {}, {}, true, appState.outlineSide == OutlineSide::Right},
        {"", MenuCommand::None, true},
        {"Theme", MenuCommand::None, false, {}, {}, true, false, std::move(themeItems)},
    };

    return {
        {"File", std::move(fileItems)},
        {"View", std::move(viewItems)},
    };
}

} // namespace mdviewer::linux_platform
