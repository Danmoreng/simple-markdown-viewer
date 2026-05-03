#include "render/theme.h"

#include <cstring>

namespace mdviewer {

ThemePalette GetThemePalette(ThemeMode theme) {
    switch (theme) {
        case ThemeMode::Sepia:
            return {
                SkColorSetRGB(232, 220, 196),
                SkColorSetRGB(123, 99, 71),
                SkColorSetRGB(73, 58, 41),
                SkColorSetRGB(55, 40, 24),
                SkColorSetRGB(118, 93, 65),
                SkColorSetRGB(146, 67, 39),
                SkColorSetRGB(124, 76, 22),
                SkColorSetARGB(120, 196, 162, 102),
                SkColorSetRGB(220, 205, 180),
                SkColorSetRGB(232, 220, 196),
                SkColorSetRGB(137, 122, 95),
                SkColorSetRGB(150, 72, 115),
                SkColorSetRGB(110, 103, 39),
                SkColorSetRGB(142, 91, 43),
                SkColorSetRGB(108, 91, 148),
                SkColorSetRGB(105, 107, 51),
                SkColorSetRGB(73, 58, 41),
                SkColorSetRGB(135, 83, 47),
                SkColorSetRGB(132, 86, 57),
                SkColorSetRGB(131, 104, 75),
                SkColorSetRGB(180, 150, 110),
                SkColorSetRGB(131, 104, 75),
                SkColorSetRGB(197, 181, 151),
                SkColorSetRGB(224, 212, 188),
                SkColorSetRGB(240, 232, 214),
                SkColorSetRGB(184, 166, 136),
                SkColorSetARGB(28, 92, 68, 37),
                SkColorSetARGB(128, 118, 88, 57),
                SkColorSetARGB(210, 118, 88, 57),
                SkColorSetARGB(70, 118, 88, 57),
                SkColorSetRGB(246, 238, 220),
                SkColorSetRGB(73, 58, 41),
                SkColorSetRGB(150, 126, 96),
                SkColorSetRGB(219, 204, 176),
                SkColorSetRGB(55, 40, 24),
                SkColorSetRGB(194, 177, 145)
            };
        case ThemeMode::Dark:
            return {
                SkColorSetRGB(31, 35, 42),
                SkColorSetRGB(122, 130, 145),
                SkColorSetRGB(224, 228, 235),
                SkColorSetRGB(248, 249, 252),
                SkColorSetRGB(160, 170, 186),
                SkColorSetRGB(255, 165, 118),
                SkColorSetRGB(120, 180, 255),
                SkColorSetARGB(125, 66, 114, 179),
                SkColorSetRGB(37, 41, 48),
                SkColorSetRGB(44, 48, 57),
                SkColorSetRGB(124, 136, 156),
                SkColorSetRGB(203, 140, 255),
                SkColorSetRGB(171, 220, 145),
                SkColorSetRGB(246, 191, 119),
                SkColorSetRGB(119, 190, 255),
                SkColorSetRGB(87, 210, 198),
                SkColorSetRGB(224, 228, 235),
                SkColorSetRGB(255, 211, 125),
                SkColorSetRGB(255, 165, 118),
                SkColorSetRGB(154, 163, 179),
                SkColorSetRGB(94, 104, 120),
                SkColorSetRGB(154, 163, 179),
                SkColorSetRGB(66, 72, 84),
                SkColorSetRGB(46, 51, 60),
                SkColorSetRGB(37, 41, 48),
                SkColorSetRGB(78, 86, 100),
                SkColorSetARGB(40, 255, 255, 255),
                SkColorSetARGB(150, 188, 194, 205),
                SkColorSetARGB(220, 188, 194, 205),
                SkColorSetARGB(70, 188, 194, 205),
                SkColorSetRGB(22, 24, 29),
                SkColorSetRGB(228, 232, 238),
                SkColorSetRGB(119, 126, 138),
                SkColorSetRGB(54, 63, 78),
                SkColorSetRGB(248, 249, 252),
                SkColorSetRGB(74, 82, 96)
            };
        case ThemeMode::Light:
        default:
            return {
                SkColorSetRGB(244, 246, 249),
                SK_ColorGRAY,
                SkColorSetRGB(36, 39, 45),
                SkColorSetRGB(28, 31, 38),
                SkColorSetRGB(86, 92, 105),
                SkColorSetRGB(165, 46, 84),
                SkColorSetRGB(26, 92, 200),
                SkColorSetARGB(110, 102, 165, 255),
                SkColorSetRGB(215, 218, 222),
                SkColorSetRGB(225, 228, 232),
                SkColorSetRGB(105, 113, 128),
                SkColorSetRGB(139, 45, 159),
                SkColorSetRGB(24, 128, 70),
                SkColorSetRGB(174, 88, 30),
                SkColorSetRGB(26, 92, 200),
                SkColorSetRGB(42, 126, 132),
                SkColorSetRGB(36, 39, 45),
                SkColorSetRGB(148, 98, 12),
                SkColorSetRGB(165, 46, 84),
                SkColorSetRGB(90, 96, 110),
                SkColorSetRGB(196, 204, 217),
                SkColorSetRGB(90, 96, 110),
                SkColorSetRGB(210, 214, 220),
                SkColorSetRGB(234, 238, 244),
                SkColorSetRGB(249, 250, 252),
                SkColorSetRGB(202, 208, 216),
                SkColorSetARGB(24, 0, 0, 0),
                SkColorSetARGB(120, 100, 100, 100),
                SkColorSetARGB(220, 100, 100, 100),
                SkColorSetARGB(65, 100, 100, 100),
                SK_ColorWHITE,
                SkColorSetRGB(40, 43, 50),
                SkColorSetRGB(145, 151, 162),
                SkColorSetRGB(225, 236, 255),
                SkColorSetRGB(28, 31, 38),
                SkColorSetRGB(214, 219, 226)
            };
    }
}

const char* ThemeModeToString(ThemeMode theme) {
    switch (theme) {
        case ThemeMode::Sepia:
            return "sepia";
        case ThemeMode::Dark:
            return "dark";
        case ThemeMode::Light:
        default:
            return "light";
    }
}

ThemeMode ThemeModeFromString(const char* value) {
    if (value == nullptr) {
        return ThemeMode::Light;
    }
    if (std::strcmp(value, "sepia") == 0) {
        return ThemeMode::Sepia;
    }
    if (std::strcmp(value, "dark") == 0) {
        return ThemeMode::Dark;
    }
    return ThemeMode::Light;
}

} // namespace mdviewer
