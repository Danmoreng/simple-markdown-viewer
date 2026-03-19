#pragma once

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkColor.h"
#pragma warning(pop)

namespace mdviewer {

enum class ThemeMode {
    Light,
    Sepia,
    Dark
};

struct ThemePalette {
    SkColor windowBackground;
    SkColor emptyStateText;
    SkColor bodyText;
    SkColor headingText;
    SkColor blockquoteText;
    SkColor codeText;
    SkColor linkText;
    SkColor selectionFill;
    SkColor codeBlockBackground;
    SkColor codeInlineBackground;
    SkColor blockquoteAccent;
    SkColor listMarker;
    SkColor thematicBreak;
    SkColor tableHeaderBackground;
    SkColor tableCellBackground;
    SkColor tableBorder;
    SkColor scrollbarTrack;
    SkColor scrollbarThumb;
    SkColor autoScrollIndicator;
    SkColor autoScrollIndicatorFill;
    SkColor menuBackground;
    SkColor menuText;
    SkColor menuDisabledText;
    SkColor menuSelectedBackground;
    SkColor menuSelectedText;
    SkColor menuSeparator;
};

ThemePalette GetThemePalette(ThemeMode theme);
const char* ThemeModeToString(ThemeMode theme);
ThemeMode ThemeModeFromString(const char* value);

} // namespace mdviewer
