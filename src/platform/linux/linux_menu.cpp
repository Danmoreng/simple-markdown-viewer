#include "platform/linux/linux_menu.h"

namespace mdviewer::linux_platform {

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
                {"Find...", MenuCommand::Find}
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
