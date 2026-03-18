#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <vector>

#include "app/app_state.h"
#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "util/file_io.h"
#include "util/skia_font_utils.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244) 
#pragma warning(disable: 4267) 
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkData.h"
#include "include/core/SkImage.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#pragma warning(pop)

namespace {
    mdviewer::AppState g_appState;
    sk_sp<SkSurface> g_surface;
    sk_sp<SkTypeface> g_typeface;
    sk_sp<SkTypeface> g_boldTypeface;
    sk_sp<SkTypeface> g_headingTypeface;
    sk_sp<SkTypeface> g_codeTypeface;
    sk_sp<SkFontMgr> g_fontMgr;
    std::wstring g_selectedFontFamily;
    HMENU g_fileMenu = nullptr;
    HMENU g_viewMenu = nullptr;
    HMENU g_themeMenu = nullptr;
    HBRUSH g_menuBackgroundBrush = nullptr;
    HFONT g_menuFont = nullptr;
    bool g_ownsMenuFont = false;
    int g_hoveredTopMenuIndex = -1;
    int g_activeTopMenuIndex = -1;

    void OnGoBack(HWND hwnd);
    void OnGoForward(HWND hwnd);
    void LoadFile(HWND hwnd, const std::filesystem::path& path, bool pushHistory = true);

    constexpr float kTextBaselineOffset = 5.0f;
    constexpr float kCodeBlockPaddingX = 12.0f;
    constexpr float kCodeBlockPaddingY = 8.0f;
    constexpr float kBlockquoteAccentWidth = 4.0f;
    constexpr float kBlockquoteTextInset = 18.0f;
    constexpr float kListMarkerGap = 16.0f;
    constexpr float kScrollbarWidth = 10.0f;
    constexpr float kScrollbarMargin = 4.0f;
    constexpr float kAutoScrollDeadZone = 2.0f;
    constexpr UINT_PTR kAutoScrollTimerId = 2001;
    constexpr UINT kAutoScrollTimerMs = 16;
    constexpr int kMenuHorizontalPadding = 12;
    constexpr int kMenuVerticalPadding = 6;
    constexpr int kMenuCheckGutterWidth = 26;
    constexpr int kMenuArrowAreaWidth = 16;
    constexpr int kMenuLabelGap = 12;
    constexpr int kMenuBarHeight = 42;
    constexpr int kMenuBarLeftInset = 12;
    constexpr int kMenuBarItemGap = 8;
    constexpr int kMenuBarTopPadding = 7;
    constexpr int kMenuBarBottomPadding = 8;
    constexpr float kTopMenuFontSize = 17.5f;
    constexpr int kInitialWindowWidth = 900;
    constexpr int kInitialWindowHeight = 1200;
    constexpr UINT_PTR kCommandOpenFile = 1001;
    constexpr UINT_PTR kCommandExit = 1002;
    constexpr UINT_PTR kCommandSelectFont = 1003;
    constexpr UINT_PTR kCommandUseDefaultFont = 1004;
    constexpr UINT_PTR kCommandThemeLight = 1101;
    constexpr UINT_PTR kCommandThemeSepia = 1102;
    constexpr UINT_PTR kCommandThemeDark = 1103;
    constexpr UINT_PTR kCommandGoBack = 1201;
    constexpr UINT_PTR kCommandGoForward = 1202;
    constexpr int kAppIconResourceId = 101;

    struct RenderContext {
        SkCanvas* canvas;
        SkPaint paint;
        SkFont font;
    };

    struct TextHit {
        size_t position = 0;
        bool valid = false;
        std::string url;
        mdviewer::InlineStyle style = mdviewer::InlineStyle::Plain;
    };

    struct MenuItemData {
        std::wstring text;
        bool isSeparator = false;
        bool hasSubmenu = false;
    };

    std::list<MenuItemData> g_menuItemData;
    std::map<std::string, sk_sp<SkImage>> g_imageCache;

    struct TopMenuItem {
        const wchar_t* label;
        HMENU menu;
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

    ThemePalette GetThemePalette(mdviewer::ThemeMode theme) {
        switch (theme) {
            case mdviewer::ThemeMode::Sepia:
                return {
                    SkColorSetRGB(232, 220, 196),
                    SkColorSetRGB(123, 99, 71),
                    SkColorSetRGB(73, 58, 41),
                    SkColorSetRGB(55, 40, 24),
                    SkColorSetRGB(118, 93, 65),
                    SkColorSetRGB(146, 67, 39),
                    SkColorSetRGB(124, 76, 22),
                    SkColorSetARGB(120, 196, 162, 102),
                    SkColorSetRGB(236, 225, 203),
                    SkColorSetRGB(232, 220, 196),
                    SkColorSetRGB(180, 150, 110),
                    SkColorSetRGB(131, 104, 75),
                    SkColorSetRGB(197, 181, 151),
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
            case mdviewer::ThemeMode::Dark:
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
                    SkColorSetRGB(94, 104, 120),
                    SkColorSetRGB(154, 163, 179),
                    SkColorSetRGB(66, 72, 84),
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
            case mdviewer::ThemeMode::Light:
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
                    SkColorSetRGB(245, 246, 248),
                    SkColorSetRGB(241, 243, 245),
                    SkColorSetRGB(196, 204, 217),
                    SkColorSetRGB(90, 96, 110),
                    SkColorSetRGB(210, 214, 220),
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

    mdviewer::ThemeMode GetCurrentThemeMode() {
        return g_appState.theme;
    }

    ThemePalette GetCurrentThemePalette() {
        return GetThemePalette(GetCurrentThemeMode());
    }

    bool EnsureFontSystem();

    std::array<TopMenuItem, 2> GetTopMenuItems() {
        return {{
            {L"File", g_fileMenu},
            {L"View", g_viewMenu},
        }};
    }

    float GetContentTopInset() {
        return static_cast<float>(kMenuBarHeight);
    }

    COLORREF ToColorRef(SkColor color) {
        return RGB(SkColorGetR(color), SkColorGetG(color), SkColorGetB(color));
    }

    HFONT GetMenuFontHandle() {
        if (g_menuFont) {
            return g_menuFont;
        }

        NONCLIENTMETRICSW metrics = {};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
            metrics.lfMenuFont.lfHeight = static_cast<LONG>(std::round(metrics.lfMenuFont.lfHeight * 1.12f));
            g_menuFont = CreateFontIndirectW(&metrics.lfMenuFont);
            g_ownsMenuFont = g_menuFont != nullptr;
        }

        if (!g_menuFont) {
            g_menuFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            g_ownsMenuFont = false;
        }

        return g_menuFont;
    }

    std::pair<std::wstring, std::wstring> SplitMenuText(const std::wstring& text) {
        const size_t tabPos = text.find(L'\t');
        if (tabPos == std::wstring::npos) {
            return {text, L""};
        }

        return {text.substr(0, tabPos), text.substr(tabPos + 1)};
    }

    SIZE MeasureMenuSegment(HDC hdc, const std::wstring& text) {
        SIZE size = {};
        if (!text.empty()) {
            GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &size);
        }
        return size;
    }

    std::wstring GetMenuItemText(HMENU menu, UINT index) {
        MENUITEMINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        if (!GetMenuItemInfoW(menu, index, TRUE, &info) || info.cch == 0) {
            return {};
        }

        std::wstring text(static_cast<size_t>(info.cch) + 1, L'\0');
        info.dwTypeData = text.data();
        info.cch = static_cast<UINT>(text.size());
        if (!GetMenuItemInfoW(menu, index, TRUE, &info)) {
            return {};
        }

        text.resize(wcslen(text.c_str()));
        return text;
    }

    void ConfigureOwnerDrawMenu(HMENU menu) {
        if (!menu) {
            return;
        }

        const int itemCount = GetMenuItemCount(menu);
        for (int index = 0; index < itemCount; ++index) {
            MENUITEMINFOW readInfo = {};
            readInfo.cbSize = sizeof(readInfo);
            readInfo.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_ID | MIIM_STRING;
            if (!GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &readInfo)) {
                continue;
            }

            auto& itemData = g_menuItemData.emplace_back();
            itemData.isSeparator = (readInfo.fType & MFT_SEPARATOR) != 0;
            itemData.hasSubmenu = readInfo.hSubMenu != nullptr;
            if (!itemData.isSeparator) {
                itemData.text = GetMenuItemText(menu, static_cast<UINT>(index));
            }

            MenuItemData* rawItemData = &itemData;

            MENUITEMINFOW writeInfo = {};
            writeInfo.cbSize = sizeof(writeInfo);
            writeInfo.fMask = MIIM_FTYPE | MIIM_DATA;
            writeInfo.fType = itemData.isSeparator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
            writeInfo.dwItemData = reinterpret_cast<ULONG_PTR>(rawItemData);
            SetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &writeInfo);

            if (readInfo.hSubMenu) {
                ConfigureOwnerDrawMenu(readInfo.hSubMenu);
            }
        }
    }

    void ApplyMenuBackgroundBrushRecursive(HMENU menu) {
        if (!menu || !g_menuBackgroundBrush) {
            return;
        }

        MENUINFO menuInfo = {};
        menuInfo.cbSize = sizeof(menuInfo);
        menuInfo.fMask = MIM_BACKGROUND;
        menuInfo.hbrBack = g_menuBackgroundBrush;
        SetMenuInfo(menu, &menuInfo);

        const int itemCount = GetMenuItemCount(menu);
        for (int index = 0; index < itemCount; ++index) {
            MENUITEMINFOW itemInfo = {};
            itemInfo.cbSize = sizeof(itemInfo);
            itemInfo.fMask = MIIM_SUBMENU;
            if (GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &itemInfo) && itemInfo.hSubMenu) {
                ApplyMenuBackgroundBrushRecursive(itemInfo.hSubMenu);
            }
        }
    }

    void RefreshMenuThemeResources(HWND hwnd) {
        if (g_menuBackgroundBrush) {
            DeleteObject(g_menuBackgroundBrush);
            g_menuBackgroundBrush = nullptr;
        }

        g_menuBackgroundBrush = CreateSolidBrush(ToColorRef(GetCurrentThemePalette().menuBackground));
        ApplyMenuBackgroundBrushRecursive(g_fileMenu);
        ApplyMenuBackgroundBrushRecursive(g_viewMenu);
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void CleanupMenuThemeResources() {
        g_hoveredTopMenuIndex = -1;
        g_activeTopMenuIndex = -1;
        if (g_menuBackgroundBrush) {
            DeleteObject(g_menuBackgroundBrush);
            g_menuBackgroundBrush = nullptr;
        }
        if (g_ownsMenuFont && g_menuFont) {
            DeleteObject(g_menuFont);
        }
        g_menuFont = nullptr;
        g_ownsMenuFont = false;
        g_menuItemData.clear();
        if (g_fileMenu) {
            DestroyMenu(g_fileMenu);
            g_fileMenu = nullptr;
        }
        if (g_viewMenu) {
            DestroyMenu(g_viewMenu);
            g_viewMenu = nullptr;
        }
        g_themeMenu = nullptr;
    }

    bool CreateMenus() {
        CleanupMenuThemeResources();

        g_fileMenu = CreatePopupMenu();
        g_viewMenu = CreatePopupMenu();
        g_themeMenu = CreatePopupMenu();
        if (!g_fileMenu || !g_viewMenu || !g_themeMenu) {
            CleanupMenuThemeResources();
            return false;
        }

        AppendMenuW(g_fileMenu, MF_STRING, kCommandOpenFile, L"&Open...\tCtrl+O");
        AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_fileMenu, MF_STRING, kCommandExit, L"E&xit");

        AppendMenuW(g_viewMenu, MF_STRING, kCommandSelectFont, L"Select &Font...");
        AppendMenuW(g_viewMenu, MF_STRING, kCommandUseDefaultFont, L"Use &Default Font");
        AppendMenuW(g_viewMenu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeLight, L"&Light");
        AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeSepia, L"&Sepia");
        AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeDark, L"&Dark");
        AppendMenuW(g_viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_themeMenu), L"&Theme");

        ConfigureOwnerDrawMenu(g_fileMenu);
        ConfigureOwnerDrawMenu(g_viewMenu);
        RefreshMenuThemeResources(nullptr);
        return true;
    }

    std::optional<std::filesystem::path> ShowOpenFileDialog(HWND hwnd) {
        wchar_t fileBuffer[MAX_PATH] = {};
        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpstrFilter =
            L"Markdown Files (*.md;*.markdown;*.txt)\0*.md;*.markdown;*.txt\0"
            L"All Files (*.*)\0*.*\0";
        dialog.lpstrFile = fileBuffer;
        dialog.nMaxFile = static_cast<DWORD>(std::size(fileBuffer));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        dialog.lpstrDefExt = L"md";

        if (!GetOpenFileNameW(&dialog)) {
            return std::nullopt;
        }

        return std::filesystem::path(fileBuffer);
    }

    std::string WideToUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0) {
            return {};
        }

        std::string utf8(static_cast<size_t>(utf8Length), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), utf8Length, nullptr, nullptr);
        return utf8;
    }

    void ResetResolvedTypefaces() {
        g_typeface.reset();
        g_boldTypeface.reset();
        g_headingTypeface.reset();
        g_codeTypeface.reset();
    }

    sk_sp<SkTypeface> CreateDocumentTypeface(SkFontStyle style) {
        if (!g_fontMgr) {
            return nullptr;
        }

        if (!g_selectedFontFamily.empty()) {
            const std::string utf8Family = WideToUtf8(g_selectedFontFamily);
            if (auto typeface = mdviewer::CreateStyledTypeface(g_fontMgr, utf8Family.c_str(), style)) {
                return typeface;
            }
        }

        return mdviewer::CreateDefaultTypeface(g_fontMgr, style);
    }

    std::optional<std::wstring> ShowFontDialog(HWND hwnd) {
        LOGFONTW logFont = {};
        if (!g_selectedFontFamily.empty()) {
            lstrcpynW(logFont.lfFaceName, g_selectedFontFamily.c_str(), LF_FACESIZE);
        }

        CHOOSEFONTW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd;
        dialog.lpLogFont = &logFont;
        dialog.Flags = CF_SCREENFONTS | CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOSIZESEL;

        if (!ChooseFontW(&dialog)) {
            return std::nullopt;
        }

        return std::wstring(logFont.lfFaceName);
    }

    void UpdateMenuState(HWND hwnd) {
        if (!g_viewMenu || !g_themeMenu) {
            return;
        }

        mdviewer::ThemeMode currentTheme = mdviewer::ThemeMode::Light;
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            currentTheme = g_appState.theme;
        }

        EnableMenuItem(
            g_viewMenu,
            kCommandUseDefaultFont,
            MF_BYCOMMAND | (g_selectedFontFamily.empty() ? MF_GRAYED : MF_ENABLED));
        CheckMenuRadioItem(
            g_themeMenu,
            kCommandThemeLight,
            kCommandThemeDark,
            currentTheme == mdviewer::ThemeMode::Sepia ? kCommandThemeSepia :
            (currentTheme == mdviewer::ThemeMode::Dark ? kCommandThemeDark : kCommandThemeLight),
            MF_BYCOMMAND);
        RefreshMenuThemeResources(hwnd);
    }

    bool HandleMeasureMenuItem(MEASUREITEMSTRUCT* measureInfo) {
        if (!measureInfo || measureInfo->CtlType != ODT_MENU) {
            return false;
        }

        const auto* itemData = reinterpret_cast<const MenuItemData*>(measureInfo->itemData);
        if (!itemData) {
            return false;
        }

        if (itemData->isSeparator) {
            measureInfo->itemWidth = 1;
            measureInfo->itemHeight = 9;
            return true;
        }

        HDC hdc = GetDC(nullptr);
        if (!hdc) {
            return false;
        }

        HGDIOBJ oldFont = SelectObject(hdc, GetMenuFontHandle());
        const auto [labelText, accelText] = SplitMenuText(itemData->text);
        const SIZE labelSize = MeasureMenuSegment(hdc, labelText);
        const SIZE accelSize = MeasureMenuSegment(hdc, accelText);
        SelectObject(hdc, oldFont);
        ReleaseDC(nullptr, hdc);

        int width = kMenuCheckGutterWidth + labelSize.cx + (kMenuHorizontalPadding * 2);
        if (!accelText.empty()) {
            width += accelSize.cx + kMenuLabelGap;
        }
        if (itemData->hasSubmenu) {
            width += kMenuArrowAreaWidth;
        }

        measureInfo->itemWidth = static_cast<UINT>(std::max(width, 150));
        measureInfo->itemHeight = static_cast<UINT>(
            std::max(std::max(static_cast<int>(labelSize.cy), static_cast<int>(accelSize.cy)) + (kMenuVerticalPadding * 2), 24));
        return true;
    }

    bool HandleDrawMenuItem(const DRAWITEMSTRUCT* drawInfo) {
        if (!drawInfo || drawInfo->CtlType != ODT_MENU) {
            return false;
        }

        const auto* itemData = reinterpret_cast<const MenuItemData*>(drawInfo->itemData);
        if (!itemData) {
            return false;
        }

        const ThemePalette palette = GetCurrentThemePalette();
        const bool isSelected = (drawInfo->itemState & ODS_SELECTED) != 0;
        const bool isDisabled = (drawInfo->itemState & ODS_DISABLED) != 0;
        const bool isChecked = (drawInfo->itemState & ODS_CHECKED) != 0;

        COLORREF backgroundColor = ToColorRef(
            (isSelected || isChecked) ? palette.menuSelectedBackground : palette.menuBackground);
        COLORREF textColor = ToColorRef(
            isDisabled ? palette.menuDisabledText : ((isSelected || isChecked) ? palette.menuSelectedText : palette.menuText));

        SetDCBrushColor(drawInfo->hDC, backgroundColor);
        FillRect(drawInfo->hDC, &drawInfo->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

        if (itemData->isSeparator) {
            RECT separatorRect = drawInfo->rcItem;
            const int midY = (separatorRect.top + separatorRect.bottom) / 2;
            HPEN pen = CreatePen(PS_SOLID, 1, ToColorRef(palette.menuSeparator));
            if (pen) {
                HGDIOBJ oldPen = SelectObject(drawInfo->hDC, pen);
                MoveToEx(drawInfo->hDC, separatorRect.left + 8, midY, nullptr);
                LineTo(drawInfo->hDC, separatorRect.right - 8, midY);
                SelectObject(drawInfo->hDC, oldPen);
                DeleteObject(pen);
            }
            return true;
        }

        SetBkMode(drawInfo->hDC, TRANSPARENT);
        SetTextColor(drawInfo->hDC, textColor);
        HGDIOBJ oldFont = SelectObject(drawInfo->hDC, GetMenuFontHandle());
        const auto [labelText, accelText] = SplitMenuText(itemData->text);

        RECT labelRect = drawInfo->rcItem;
        labelRect.left += kMenuCheckGutterWidth;
        labelRect.right -= kMenuHorizontalPadding;

        if (itemData->hasSubmenu) {
            labelRect.right -= kMenuArrowAreaWidth;
        }

        if (!accelText.empty()) {
            RECT accelRect = labelRect;
            DrawTextW(
                drawInfo->hDC,
                accelText.c_str(),
                static_cast<int>(accelText.size()),
                &accelRect,
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            HDC measureDc = drawInfo->hDC;
            const SIZE accelSize = MeasureMenuSegment(measureDc, accelText);
            labelRect.right -= accelSize.cx + kMenuLabelGap;
        }

        DrawTextW(
            drawInfo->hDC,
            labelText.c_str(),
            static_cast<int>(labelText.size()),
            &labelRect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(drawInfo->hDC, oldFont);
        return true;
    }

    std::vector<RECT> GetTopMenuItemRects(HWND hwnd) {
        RECT clientRect = {};
        GetClientRect(hwnd, &clientRect);

        std::vector<RECT> rects;
        int currentX = kMenuBarLeftInset;

        HDC hdc = GetDC(hwnd);
        if (!hdc) {
            return rects;
        }

        HGDIOBJ oldFont = SelectObject(hdc, GetMenuFontHandle());
        for (const auto& item : GetTopMenuItems()) {
            SIZE size = {};
            GetTextExtentPoint32W(hdc, item.label, static_cast<int>(wcslen(item.label)), &size);
            RECT rect = {
                currentX,
                0,
                currentX + size.cx + (kMenuHorizontalPadding * 2),
                kMenuBarHeight
            };
            rects.push_back(rect);
            currentX = rect.right + kMenuBarItemGap;
        }

        SelectObject(hdc, oldFont);
        ReleaseDC(hwnd, hdc);
        return rects;
    }

    void DrawArrow(SkCanvas* canvas, float x, float y, float size, bool right, SkColor color) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(color);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(2.0f);
        paint.setStrokeCap(SkPaint::kRound_Cap);
        paint.setStrokeJoin(SkPaint::kRound_Join);

        const float halfSize = size * 0.5f;
        const float tipX = right ? x + halfSize : x - halfSize;
        const float tailX = right ? x - halfSize : x + halfSize;

        SkPathBuilder builder;
        builder.moveTo(tailX, y);
        builder.lineTo(tipX, y);
        builder.moveTo(tipX - (right ? 4 : -4), y - 4);
        builder.lineTo(tipX, y);
        builder.lineTo(tipX - (right ? 4 : -4), y + 4);
        canvas->drawPath(builder.detach(), paint);
    }

    int HitTestTopMenu(HWND hwnd, int x, int y) {
        if (y < 0 || y >= kMenuBarHeight) {
            return -1;
        }

        const auto rects = GetTopMenuItemRects(hwnd);
        for (size_t index = 0; index < rects.size(); ++index) {
            const RECT& rect = rects[index];
            if (x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom) {
                return static_cast<int>(index);
            }
        }

        // Toolbar buttons at the right
        const float rightEdge = static_cast<float>(g_surface->width()) - 12.0f;
        const float btnSize = 34.0f;
        const float forwardBtnX = rightEdge - btnSize;
        const float backBtnX = forwardBtnX - btnSize - 4.0f;
        const float btnY = (static_cast<float>(kMenuBarHeight) - btnSize) * 0.5f;

        if (y >= btnY && y < btnY + btnSize) {
            if (x >= backBtnX && x < backBtnX + btnSize) return -2;
            if (x >= forwardBtnX && x < forwardBtnX + btnSize) return -3;
        }

        return -1;
    }

    void DrawTopMenuBar(SkCanvas* canvas, HWND hwnd) {
        const ThemePalette palette = GetCurrentThemePalette();

        SkPaint backgroundPaint;
        backgroundPaint.setAntiAlias(false);
        backgroundPaint.setColor(palette.menuBackground);
        canvas->drawRect(
            SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(g_surface->width()), static_cast<float>(kMenuBarHeight)),
            backgroundPaint);

        if (!EnsureFontSystem()) {
            return;
        }

        SkFont menuFont;
        menuFont.setTypeface(g_typeface);
        menuFont.setSize(kTopMenuFontSize);
        menuFont.setHinting(SkFontHinting::kSlight);
        menuFont.setSubpixel(true);
        menuFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
        SkFontMetrics menuMetrics;
        menuFont.getMetrics(&menuMetrics);
        const float menuTextHeight = menuMetrics.fDescent - menuMetrics.fAscent;
        const float menuBaselineY =
            std::round(((static_cast<float>(kMenuBarHeight) - menuTextHeight) * 0.5f) - menuMetrics.fAscent);

        SkPaint textPaint;
        textPaint.setAntiAlias(true);

        const auto rects = GetTopMenuItemRects(hwnd);
        const auto items = GetTopMenuItems();

        for (size_t index = 0; index < rects.size() && index < items.size(); ++index) {
            const bool isHighlighted = static_cast<int>(index) == g_hoveredTopMenuIndex ||
                                       static_cast<int>(index) == g_activeTopMenuIndex;
            const RECT& rect = rects[index];

            if (isHighlighted) {
                SkPaint highlightPaint;
                highlightPaint.setAntiAlias(true);
                highlightPaint.setColor(palette.menuSelectedBackground);
                canvas->drawRoundRect(
                    SkRect::MakeLTRB(
                        static_cast<float>(rect.left),
                        4.0f,
                        static_cast<float>(rect.right),
                        static_cast<float>(kMenuBarHeight - 4)),
                    6.0f,
                    6.0f,
                    highlightPaint);
            }

            textPaint.setColor(isHighlighted ? palette.menuSelectedText : palette.menuText);
            const float textX = static_cast<float>(rect.left + kMenuHorizontalPadding);
            canvas->drawSimpleText(
                items[index].label,
                wcslen(items[index].label) * sizeof(wchar_t),
                SkTextEncoding::kUTF16,
                textX,
                menuBaselineY,
                menuFont,
                textPaint);
        }

        // Draw toolbar buttons (Back/Forward) at the right
        const float rightEdge = static_cast<float>(g_surface->width()) - 12.0f;
        const float btnSize = 34.0f;
        const float forwardBtnX = rightEdge - btnSize;
        const float backBtnX = forwardBtnX - btnSize - 4.0f;
        const float btnY = (static_cast<float>(kMenuBarHeight) - btnSize) * 0.5f;

        auto DrawToolbarBtn = [&](float x, float y, bool enabled, int hoverIdx, bool right) {
            const bool isHovered = g_hoveredTopMenuIndex == hoverIdx;
            if (enabled && isHovered) {
                SkPaint hPaint;
                hPaint.setAntiAlias(true);
                hPaint.setColor(palette.menuSelectedBackground);
                canvas->drawRoundRect(SkRect::MakeXYWH(x, y, btnSize, btnSize), 6.0f, 6.0f, hPaint);
            }
            SkColor color = enabled ? (isHovered ? palette.menuSelectedText : palette.menuText) : palette.menuDisabledText;
            DrawArrow(canvas, x + btnSize * 0.5f, y + btnSize * 0.5f, 14.0f, right, color);
        };

        DrawToolbarBtn(backBtnX, btnY, g_appState.CanGoBack(), -2, false);
        DrawToolbarBtn(forwardBtnX, btnY, g_appState.CanGoForward(), -3, true);
    }

    void OpenTopMenu(HWND hwnd, int menuIndex) {
        if (menuIndex == -2) {
            if (g_appState.CanGoBack()) OnGoBack(hwnd);
            return;
        }
        if (menuIndex == -3) {
            if (g_appState.CanGoForward()) OnGoForward(hwnd);
            return;
        }

        const auto items = GetTopMenuItems();
        const auto rects = GetTopMenuItemRects(hwnd);
        if (menuIndex < 0 || static_cast<size_t>(menuIndex) >= items.size() || static_cast<size_t>(menuIndex) >= rects.size()) {
            return;
        }

        HMENU menu = items[menuIndex].menu;
        if (!menu) {
            return;
        }

        g_activeTopMenuIndex = menuIndex;
        InvalidateRect(hwnd, nullptr, FALSE);

        POINT screenPoint = {rects[menuIndex].left, rects[menuIndex].bottom};
        ClientToScreen(hwnd, &screenPoint);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
            screenPoint.x,
            screenPoint.y,
            hwnd,
            nullptr);

        g_activeTopMenuIndex = -1;
        g_hoveredTopMenuIndex = -1;
        InvalidateRect(hwnd, nullptr, FALSE);

        if (command != 0) {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
    }

    bool EnsureFontSystem() {
        if (!g_fontMgr) {
            g_fontMgr = mdviewer::CreateFontManager();
        }

        if (!g_typeface) {
            g_typeface = CreateDocumentTypeface(SkFontStyle::Normal());
        }

        if (!g_boldTypeface) {
            g_boldTypeface = CreateDocumentTypeface(SkFontStyle::Bold());
            if (!g_boldTypeface) {
                g_boldTypeface = g_typeface;
            }
        }

        if (!g_headingTypeface) {
            g_headingTypeface = g_boldTypeface;
            if (!g_headingTypeface) {
                g_headingTypeface = g_typeface;
            }
        }

        if (!g_codeTypeface) {
            g_codeTypeface = mdviewer::CreatePreferredTypeface(
                g_fontMgr,
                {"Cascadia Mono", "Consolas", "JetBrains Mono", "Courier New"});
            if (!g_codeTypeface) {
                g_codeTypeface = g_typeface;
            }
        }

        return g_fontMgr != nullptr &&
               g_typeface != nullptr &&
               g_boldTypeface != nullptr &&
               g_headingTypeface != nullptr &&
               g_codeTypeface != nullptr;
    }

    bool IsHeading(mdviewer::BlockType blockType) {
        return blockType == mdviewer::BlockType::Heading1 ||
               blockType == mdviewer::BlockType::Heading2 ||
               blockType == mdviewer::BlockType::Heading3 ||
               blockType == mdviewer::BlockType::Heading4 ||
               blockType == mdviewer::BlockType::Heading5 ||
               blockType == mdviewer::BlockType::Heading6;
    }

    float GetBlockFontSize(mdviewer::BlockType blockType) {
        switch (blockType) {
            case mdviewer::BlockType::Heading1: return 34.0f;
            case mdviewer::BlockType::Heading2: return 28.0f;
            case mdviewer::BlockType::Heading3: return 22.0f;
            case mdviewer::BlockType::Heading4: return 19.0f;
            case mdviewer::BlockType::Heading5: return 17.0f;
            case mdviewer::BlockType::Heading6: return 16.0f;
            case mdviewer::BlockType::CodeBlock: return 15.0f;
            default: return 17.0f;
        }
    }

    const mdviewer::LineLayout* FindFirstLine(const mdviewer::BlockLayout& block) {
        if (!block.lines.empty()) {
            return &block.lines.front();
        }

        for (const auto& child : block.children) {
            if (const auto* line = FindFirstLine(child)) {
                return line;
            }
        }

        return nullptr;
    }

    void ConfigureFont(RenderContext& ctx, mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        const bool isCode = blockType == mdviewer::BlockType::CodeBlock || inlineStyle == mdviewer::InlineStyle::Code;
        const bool isHeading = IsHeading(blockType);
        const bool isStrong = inlineStyle == mdviewer::InlineStyle::Strong;
        ctx.font.setTypeface(
            isCode ? g_codeTypeface : (isHeading ? g_headingTypeface : (isStrong ? g_boldTypeface : g_typeface)));
        ctx.font.setSize(isCode ? GetBlockFontSize(mdviewer::BlockType::CodeBlock) : GetBlockFontSize(blockType));
        ctx.font.setSubpixel(!isHeading);
        ctx.font.setHinting(SkFontHinting::kSlight);
        ctx.font.setEdging(isHeading ? SkFont::Edging::kAntiAlias : SkFont::Edging::kSubpixelAntiAlias);
        ctx.font.setEmbolden(false);
        ctx.font.setSkewX(inlineStyle == mdviewer::InlineStyle::Emphasis ? -0.18f : 0.0f);
        ctx.font.setScaleX(1.0f);
    }

    SkColor GetTextColor(mdviewer::BlockType blockType, mdviewer::InlineStyle inlineStyle) {
        const ThemePalette palette = GetCurrentThemePalette();
        if (blockType == mdviewer::BlockType::Blockquote) {
            return palette.blockquoteText;
        }
        if (inlineStyle == mdviewer::InlineStyle::Code) {
            return palette.codeText;
        }
        if (inlineStyle == mdviewer::InlineStyle::Link) {
            return palette.linkText;
        }
        if (IsHeading(blockType)) {
            return palette.headingText;
        }
        return palette.bodyText;
    }

    float GetContentX(const mdviewer::BlockLayout& block) {
        if (block.type == mdviewer::BlockType::CodeBlock) {
            return block.bounds.left() + kCodeBlockPaddingX;
        }
        if (block.type == mdviewer::BlockType::Blockquote) {
            return block.bounds.left() + kBlockquoteTextInset;
        }
        return block.bounds.left();
    }

    size_t GetSelectionStart() {
        return std::min(g_appState.selectionAnchor, g_appState.selectionFocus);
    }

    size_t GetSelectionEnd() {
        return std::max(g_appState.selectionAnchor, g_appState.selectionFocus);
    }

    float GetViewportHeight(HWND hwnd) {
        RECT rect;
        GetClientRect(hwnd, &rect);
        return std::max(static_cast<float>(rect.bottom - rect.top) - GetContentTopInset(), 0.0f);
    }

    float GetMaxScroll(HWND hwnd) {
        return std::max(g_appState.docLayout.totalHeight - GetViewportHeight(hwnd), 0.0f);
    }

    void ClampScrollOffset(HWND hwnd) {
        g_appState.scrollOffset = std::clamp(g_appState.scrollOffset, 0.0f, GetMaxScroll(hwnd));
    }

    void StopAutoScroll(HWND hwnd) {
        const bool shouldReleaseCapture = GetCapture() == hwnd;
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isAutoScrolling = false;
            g_appState.autoScrollOriginX = 0.0f;
            g_appState.autoScrollOriginY = 0.0f;
            g_appState.autoScrollCursorX = 0.0f;
            g_appState.autoScrollCursorY = 0.0f;
        }
        KillTimer(hwnd, kAutoScrollTimerId);
        if (shouldReleaseCapture) {
            ReleaseCapture();
        }
    }

    void StartAutoScroll(HWND hwnd, int x, int y) {
        {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isAutoScrolling = true;
            g_appState.isSelecting = false;
            g_appState.isDraggingScrollbar = false;
            g_appState.autoScrollOriginX = static_cast<float>(x);
            g_appState.autoScrollOriginY = static_cast<float>(y);
            g_appState.autoScrollCursorX = static_cast<float>(x);
            g_appState.autoScrollCursorY = static_cast<float>(y);
        }
        SetCapture(hwnd);
        SetTimer(hwnd, kAutoScrollTimerId, kAutoScrollTimerMs, nullptr);
    }

    float ComputeAutoScrollVelocity(float delta) {
        const float magnitude = std::abs(delta);
        if (magnitude <= kAutoScrollDeadZone) {
            return 0.0f;
        }

        const float adjusted = magnitude - kAutoScrollDeadZone;
        const float normalized = std::min(adjusted / 16.0f, 25.0f);
        const float speed = (normalized * normalized) * 0.55f + (adjusted * 0.22f);
        return delta < 0.0f ? -speed : speed;
    }

    bool TickAutoScroll(HWND hwnd) {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        if (!g_appState.isAutoScrolling) {
            return false;
        }

        const float deltaY = g_appState.autoScrollCursorY - g_appState.autoScrollOriginY;
        const float scrollDelta = ComputeAutoScrollVelocity(deltaY);
        if (scrollDelta == 0.0f) {
            return false;
        }

        const float previousOffset = g_appState.scrollOffset;
        g_appState.scrollOffset = std::clamp(g_appState.scrollOffset + scrollDelta, 0.0f, GetMaxScroll(hwnd));
        return std::abs(g_appState.scrollOffset - previousOffset) > 0.01f;
    }

    void DrawAutoScrollIndicator(SkCanvas* canvas) {
        const ThemePalette palette = GetCurrentThemePalette();
        if (!g_appState.isAutoScrolling) {
            return;
        }
        const float originX = g_appState.autoScrollOriginX;
        const float originY = g_appState.autoScrollOriginY;

        SkPaint fillPaint;
        fillPaint.setAntiAlias(true);
        fillPaint.setColor(palette.autoScrollIndicatorFill);
        canvas->drawCircle(originX, originY, 15.0f, fillPaint);

        SkPaint ringPaint;
        ringPaint.setAntiAlias(true);
        ringPaint.setColor(palette.autoScrollIndicator);
        ringPaint.setStyle(SkPaint::kStroke_Style);
        ringPaint.setStrokeWidth(1.8f);
        canvas->drawCircle(originX, originY, 15.0f, ringPaint);
        canvas->drawLine(originX - 7.0f, originY, originX + 7.0f, originY, ringPaint);
        canvas->drawLine(originX, originY - 7.0f, originX, originY + 7.0f, ringPaint);

        SkPaint arrowPaint;
        arrowPaint.setAntiAlias(true);
        arrowPaint.setColor(palette.autoScrollIndicator);
        arrowPaint.setStyle(SkPaint::kStroke_Style);
        arrowPaint.setStrokeWidth(1.8f);

        canvas->drawLine(originX - 4.0f, originY - 7.0f, originX, originY - 11.0f, arrowPaint);
        canvas->drawLine(originX, originY - 11.0f, originX + 4.0f, originY - 7.0f, arrowPaint);
        canvas->drawLine(originX - 4.0f, originY + 7.0f, originX, originY + 11.0f, arrowPaint);
        canvas->drawLine(originX, originY + 11.0f, originX + 4.0f, originY + 7.0f, arrowPaint);
    }

    std::optional<SkRect> GetScrollbarThumbRect(HWND hwnd) {
        const float viewportHeight = GetViewportHeight(hwnd);
        if (g_appState.docLayout.totalHeight <= viewportHeight || viewportHeight <= 0.0f) {
            return std::nullopt;
        }

        const float scrollRatio = viewportHeight / g_appState.docLayout.totalHeight;
        const float thumbHeight = std::max(viewportHeight * scrollRatio, 24.0f);
        const float maxThumbTravel = viewportHeight - thumbHeight;
        const float maxScroll = GetMaxScroll(hwnd);
        const float thumbY = maxScroll > 0.0f ? (g_appState.scrollOffset / maxScroll) * maxThumbTravel : 0.0f;

        return SkRect::MakeXYWH(
            g_surface->width() - kScrollbarWidth - kScrollbarMargin,
            thumbY + kScrollbarMargin + GetContentTopInset(),
            kScrollbarWidth,
            thumbHeight);
    }

    size_t FindTextPositionInRun(RenderContext& ctx, mdviewer::BlockType blockType, const mdviewer::RunLayout& run, float xInRun) {
        ConfigureFont(ctx, blockType, run.style);

        if (xInRun <= 0.0f) {
            return run.textStart;
        }

        float bestDistance = std::numeric_limits<float>::max();
        size_t bestOffset = run.text.size();

        for (size_t offset = 0; offset <= run.text.size(); ) {
            const float width = ctx.font.measureText(run.text.c_str(), offset, SkTextEncoding::kUTF8);
            const float distance = std::abs(width - xInRun);
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestOffset = offset;
            }

            if (offset == run.text.size()) {
                break;
            }

            offset++;
            while (offset < run.text.size() && (static_cast<unsigned char>(run.text[offset]) & 0xC0) == 0x80) {
                offset++;
            }
        }

        return run.textStart + bestOffset;
    }

    void DrawSelectionForLine(RenderContext& ctx, const mdviewer::BlockLayout& block, const mdviewer::LineLayout& line) {
        if (!g_appState.HasSelection()) {
            return;
        }

        const ThemePalette palette = GetCurrentThemePalette();

        const size_t selectionStart = GetSelectionStart();
        const size_t selectionEnd = GetSelectionEnd();
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            const size_t runStart = run.textStart;
            const size_t runEnd = run.textStart + run.text.size();

            if (selectionEnd <= runStart || selectionStart >= runEnd) {
                ConfigureFont(ctx, block.type, run.style);
                currentX += ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                continue;
            }

            ConfigureFont(ctx, block.type, run.style);
            const size_t highlightStart = std::max(selectionStart, runStart) - runStart;
            const size_t highlightEnd = std::min(selectionEnd, runEnd) - runStart;
            const float highlightLeft = currentX + ctx.font.measureText(run.text.c_str(), highlightStart, SkTextEncoding::kUTF8);
            const float highlightRight = currentX + ctx.font.measureText(run.text.c_str(), highlightEnd, SkTextEncoding::kUTF8);

            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(palette.selectionFill);
            ctx.canvas->drawRect(
                SkRect::MakeLTRB(highlightLeft, line.y + 1.0f, highlightRight, line.y + line.height - 1.0f),
                highlightPaint);

            currentX += ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
        }
    }

    bool FindBestHit(RenderContext& ctx,
                     const std::vector<mdviewer::BlockLayout>& blocks,
                     float x,
                     float documentY,
                     TextHit& bestHit,
                     float& bestDistance) {
        for (const auto& block : blocks) {
            for (const auto& line : block.lines) {
                const float lineTop = line.y;
                const float lineBottom = line.y + line.height;
                float distance = 0.0f;
                if (documentY < lineTop) {
                    distance = lineTop - documentY;
                } else if (documentY > lineBottom) {
                    distance = documentY - lineBottom;
                }

                if (distance > bestDistance) {
                    continue;
                }

                float currentX = GetContentX(block);
                size_t fallbackPosition = line.textStart;
                bool foundRun = false;

                for (const auto& run : line.runs) {
                    ConfigureFont(ctx, block.type, run.style);
                    const float runWidth = ctx.font.measureText(run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8);
                    const float runEndX = currentX + runWidth;
                    fallbackPosition = run.textStart + run.text.size();

                    if (x <= runEndX || &run == &line.runs.back()) {
                        bestHit.position = FindTextPositionInRun(ctx, block.type, run, x - currentX);
                        bestHit.valid = true;
                        bestHit.url = run.url;
                        bestHit.style = run.style;
                        bestDistance = distance;
                        foundRun = true;
                        break;
                    }

                    currentX = runEndX;
                }

                if (!foundRun && line.runs.empty()) {
                    bestHit.position = line.textStart;
                    bestHit.valid = true;
                    bestDistance = distance;
                } else if (!foundRun && fallbackPosition >= line.textStart) {
                    bestHit.position = fallbackPosition;
                    bestHit.valid = true;
                    bestDistance = distance;
                }
            }

            if (FindBestHit(ctx, block.children, x, documentY, bestHit, bestDistance)) {
                // bestHit updated by recursion
            }
        }

        return bestHit.valid;
    }

    TextHit HitTestText(float x, float viewportY) {
        TextHit hit;
        if (!EnsureFontSystem()) {
            return hit;
        }
        viewportY -= GetContentTopInset();
        if (viewportY < 0.0f) {
            return hit;
        }

        RenderContext ctx;
        ctx.canvas = nullptr;
        ctx.paint.setAntiAlias(true);
        ctx.font.setTypeface(g_typeface);

        const float documentY = viewportY + g_appState.scrollOffset;
        float bestDistance = std::numeric_limits<float>::max();
        FindBestHit(ctx, g_appState.docLayout.blocks, x, documentY, hit, bestDistance);
        return hit;
    }

    bool CopyTextToClipboard(HWND hwnd, const std::string& utf8Text) {
        if (utf8Text.empty()) {
            return false;
        }

        const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), nullptr, 0);
        if (wideLength <= 0) {
            return false;
        }

        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wideLength + 1) * sizeof(wchar_t));
        if (!memory) {
            return false;
        }

        auto* wideText = static_cast<wchar_t*>(GlobalLock(memory));
        if (!wideText) {
            GlobalFree(memory);
            return false;
        }

        MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), static_cast<int>(utf8Text.size()), wideText, wideLength);
        wideText[wideLength] = L'\0';
        GlobalUnlock(memory);

        if (!OpenClipboard(hwnd)) {
            GlobalFree(memory);
            return false;
        }

        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) {
            CloseClipboard();
            GlobalFree(memory);
            return false;
        }

        CloseClipboard();
        return true;
    }

    void DrawBlockDecoration(RenderContext& ctx,
                             const mdviewer::BlockLayout& block,
                             mdviewer::BlockType parentType,
                             size_t siblingIndex) {
        const ThemePalette palette = GetCurrentThemePalette();

        if (block.type == mdviewer::BlockType::CodeBlock) {
            SkPaint backgroundPaint;
            backgroundPaint.setAntiAlias(true);
            backgroundPaint.setColor(palette.codeBlockBackground);
            ctx.canvas->drawRoundRect(
                SkRect::MakeLTRB(
                    block.bounds.left() - kCodeBlockPaddingX,
                    block.bounds.top() - kCodeBlockPaddingY,
                    block.bounds.right() + kCodeBlockPaddingX,
                    block.bounds.bottom() + kCodeBlockPaddingY),
                10.0f,
                10.0f,
                backgroundPaint);
        } else if (block.type == mdviewer::BlockType::Blockquote) {
            SkPaint accentPaint;
            accentPaint.setAntiAlias(true);
            accentPaint.setColor(palette.blockquoteAccent);
            ctx.canvas->drawRoundRect(
                SkRect::MakeXYWH(
                    block.bounds.left(),
                    block.bounds.top(),
                    kBlockquoteAccentWidth,
                    std::max(block.bounds.height(), 12.0f)),
                2.0f,
                2.0f,
                accentPaint);
        } else if (block.type == mdviewer::BlockType::ListItem &&
                   (parentType == mdviewer::BlockType::UnorderedList || parentType == mdviewer::BlockType::OrderedList)) {
            const mdviewer::LineLayout* firstLine = FindFirstLine(block);
            if (!firstLine) {
                return;
            }

            ConfigureFont(ctx, mdviewer::BlockType::Paragraph, mdviewer::InlineStyle::Plain);
            ctx.paint.setColor(palette.listMarker);
            const float markerBaseline = firstLine->y + firstLine->height - kTextBaselineOffset;
            const float markerX = block.bounds.left() - kListMarkerGap;

            if (parentType == mdviewer::BlockType::OrderedList) {
                const std::string marker = std::to_string(siblingIndex + 1) + ".";
                ctx.canvas->drawString(marker.c_str(), markerX - 6.0f, markerBaseline, ctx.font, ctx.paint);
            } else {
                ctx.canvas->drawCircle(markerX, markerBaseline - (firstLine->height * 0.35f), 3.0f, ctx.paint);
            }
        }
    }

    void DrawLine(RenderContext& ctx, const mdviewer::BlockLayout& block, const mdviewer::LineLayout& line) {
        const ThemePalette palette = GetCurrentThemePalette();
        float currentX = GetContentX(block);

        for (const auto& run : line.runs) {
            ConfigureFont(ctx, block.type, run.style);

            SkRect textBounds;
            const float advance = ctx.font.measureText(
                run.text.c_str(), run.text.size(), SkTextEncoding::kUTF8, &textBounds);
            const float baselineY = std::round(line.y + line.height - kTextBaselineOffset);

            if (run.style == mdviewer::InlineStyle::Image && !run.url.empty()) {
                sk_sp<SkImage> image;
                auto it = g_imageCache.find(run.url);
                if (it != g_imageCache.end()) {
                    image = it->second;
                } else {
                    // Try to load image
                    std::filesystem::path imagePath = run.url;
                    if (imagePath.is_relative()) {
                        imagePath = g_appState.currentFilePath.parent_path() / run.url;
                    }
                    
                    if (std::filesystem::exists(imagePath)) {
                        auto data = SkData::MakeFromFileName(imagePath.string().c_str());
                        if (data) {
                            image = SkImages::DeferredFromEncodedData(data);
                            if (image) {
                                g_imageCache[run.url] = image;
                                // After loading, we might want to relayout if actual size is different,
                                // but for now we used a placeholder size.
                            }
                        }
                    }
                }

                float displayW = run.imageWidth;
                float displayH = run.imageHeight;
                
                // If it's a large block image (scaled to ~90% width), center it
                float drawX = currentX;
                const float blockW = block.bounds.width();
                if (displayW > blockW * 0.8f) {
                    drawX = block.bounds.left() + (blockW - displayW) * 0.5f;
                }

                if (image) {
                    ctx.canvas->drawImageRect(image, 
                        SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH),
                        SkSamplingOptions(SkFilterMode::kLinear));
                } else {
                    // Draw placeholder
                    SkPaint placeholderPaint;
                    placeholderPaint.setStyle(SkPaint::kStroke_Style);
                    placeholderPaint.setColor(palette.listMarker);
                    placeholderPaint.setStrokeWidth(1.0f);
                    SkRect rect = SkRect::MakeXYWH(drawX, line.y + (line.height - displayH) / 2.0f, displayW, displayH);
                    ctx.canvas->drawRect(rect, placeholderPaint);
                    ctx.canvas->drawLine(rect.left(), rect.top(), rect.right(), rect.bottom(), placeholderPaint);
                    ctx.canvas->drawLine(rect.right(), rect.top(), rect.left(), rect.bottom(), placeholderPaint);
                }
                currentX += displayW + 4.0f;
                continue;
            }

            if (run.style == mdviewer::InlineStyle::Code && !run.text.empty()) {
                SkPaint chipPaint;
                chipPaint.setAntiAlias(true);
                chipPaint.setColor(palette.codeInlineBackground);
                ctx.canvas->drawRoundRect(
                    SkRect::MakeLTRB(
                        currentX - 4.0f,
                        line.y + 1.0f,
                        currentX + advance + 4.0f,
                        line.y + line.height - 1.0f),
                    4.0f,
                    4.0f,
                    chipPaint);
            }

            ctx.paint.setColor(GetTextColor(block.type, run.style));
            ctx.canvas->drawString(run.text.c_str(), currentX, baselineY, ctx.font, ctx.paint);

            if (run.style == mdviewer::InlineStyle::Link && advance > 0.0f) {
                SkPaint underlinePaint;
                underlinePaint.setAntiAlias(true);
                underlinePaint.setStrokeWidth(1.0f);
                underlinePaint.setColor(GetTextColor(block.type, run.style));
                ctx.canvas->drawLine(currentX, baselineY + 2.0f, currentX + advance, baselineY + 2.0f, underlinePaint);
            }

            currentX += advance;
        }
    }
    void PreloadImage(const std::string& url, const std::filesystem::path& baseDir) {
        if (g_imageCache.find(url) != g_imageCache.end()) {
            return;
        }

        std::filesystem::path imagePath = url;
        if (imagePath.is_relative()) {
            imagePath = baseDir / url;
        }

        if (std::filesystem::exists(imagePath)) {
            auto data = SkData::MakeFromFileName(imagePath.string().c_str());
            if (data) {
                auto image = SkImages::DeferredFromEncodedData(data);
                if (image) {
                    g_imageCache[url] = image;
                }
            }
        }
    }

    void PreloadImages(const mdviewer::DocumentModel& doc, const std::filesystem::path& baseDir) {
        for (const auto& block : doc.blocks) {
            for (const auto& run : block.inlineRuns) {
                if (run.style == mdviewer::InlineStyle::Image) {
                    PreloadImage(run.url, baseDir);
                }
            }
            // For nested blocks (like lists or blockquotes)
            if (!block.children.empty()) {
                mdviewer::DocumentModel subDoc;
                subDoc.blocks = block.children;
                PreloadImages(subDoc, baseDir);
            }
            }
            }

            void DrawBlocks(RenderContext& ctx,
                    const std::vector<mdviewer::BlockLayout>& blocks,
                    mdviewer::BlockType parentType = mdviewer::BlockType::Paragraph) {
            for (size_t index = 0; index < blocks.size(); ++index) {
            const auto& block = blocks[index];
            if (block.type == mdviewer::BlockType::ThematicBreak) {
                ctx.paint.setColor(GetCurrentThemePalette().thematicBreak);
                ctx.paint.setStrokeWidth(1.0f);
                ctx.canvas->drawLine(block.bounds.left(), block.bounds.centerY(), block.bounds.right(), block.bounds.centerY(), ctx.paint);
            } else {
                DrawBlockDecoration(ctx, block, parentType, index);

                for (const auto& line : block.lines) {
                    DrawSelectionForLine(ctx, block, line);
                    DrawLine(ctx, block, line);
                }

                if (!block.children.empty()) {
                    DrawBlocks(ctx, block.children, block.type);
                }
            }
            }
            }

            void UpdateSurface(HWND hwnd) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            if (width <= 0 || height <= 0) return;

            SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
            g_surface = SkSurfaces::Raster(info);
            }

            void Render(HWND hwnd) {
            if (!g_surface) return;

            if (!EnsureFontSystem()) {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return;
            }

            SkCanvas* canvas = g_surface->getCanvas();
            const ThemePalette palette = GetCurrentThemePalette();
            canvas->clear(palette.windowBackground);

            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);

            RenderContext ctx;
            ctx.canvas = canvas;
            ctx.paint.setAntiAlias(true);
            ctx.font.setTypeface(g_typeface);
            ctx.font.setSubpixel(true);
            ctx.font.setHinting(SkFontHinting::kSlight);
            ctx.font.setEdging(SkFont::Edging::kSubpixelAntiAlias);

            canvas->save();
            canvas->clipRect(SkRect::MakeLTRB(
                0.0f,
                GetContentTopInset(),
                static_cast<float>(g_surface->width()),
                static_cast<float>(g_surface->height())));
            canvas->translate(0, GetContentTopInset() - g_appState.scrollOffset);

            DrawBlocks(ctx, g_appState.docLayout.blocks);

            if (g_appState.sourceText.empty()) {
                ctx.font.setSize(20.0f);
                ctx.paint.setColor(palette.emptyStateText);
                const char* msg = "Drag and drop a Markdown file here";
                SkRect bounds;
                ctx.font.measureText(msg, strlen(msg), SkTextEncoding::kUTF8, &bounds);
                const float emptyStateY = GetViewportHeight(hwnd) * 0.5f;
                canvas->drawString(msg, (g_surface->width() - bounds.width()) / 2, emptyStateY, ctx.font, ctx.paint);
            }

            canvas->restore();

            DrawTopMenuBar(canvas, hwnd);

            // Draw simple scrollbar indicator
            if (const auto thumb = GetScrollbarThumbRect(hwnd)) {
                SkPaint trackPaint;
                trackPaint.setAntiAlias(true);
                trackPaint.setColor(palette.scrollbarTrack);
                ctx.canvas->drawRoundRect(
                    SkRect::MakeXYWH(
                        g_surface->width() - kScrollbarWidth - kScrollbarMargin,
                        kScrollbarMargin + GetContentTopInset(),
                        kScrollbarWidth,
                        std::max(GetViewportHeight(hwnd) - (kScrollbarMargin * 2.0f), 1.0f)),
                    5.0f,
                    5.0f,
                    trackPaint);

                ctx.paint.setColor(palette.scrollbarThumb);
                ctx.canvas->drawRoundRect(*thumb, 5.0f, 5.0f, ctx.paint);
            }

            DrawAutoScrollIndicator(ctx.canvas);
            }

            SkPixmap pixmap;
            if (g_surface->peekPixels(&pixmap)) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = pixmap.width();
            bmi.bmiHeader.biHeight = -pixmap.height();
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            StretchDIBits(hdc, 0, 0, pixmap.width(), pixmap.height(),
                        0, 0, pixmap.width(), pixmap.height(),
                        pixmap.addr(), &bmi, DIB_RGB_COLORS, SRCCOPY);

            EndPaint(hwnd, &ps);
            } else {
            // Fallback Begin/EndPaint to clear update region if peekPixels fails
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            }
            }

            void LoadFile(HWND hwnd, const std::filesystem::path& path, bool pushHistory) {
            if (!EnsureFontSystem()) {
            MessageBoxW(hwnd, L"Font initialization failed. The document cannot be rendered.", L"Error", MB_ICONERROR);
            return;
            }

            auto content = mdviewer::ReadFileToString(path);
            if (content) {
            auto docModel = mdviewer::MarkdownParser::Parse(*content);

            // Pre-load images to get their sizes for layout
            PreloadImages(docModel, path.parent_path());

            RECT rect;
            GetClientRect(hwnd, &rect);
            float width = static_cast<float>(rect.right - rect.left);

            auto imageSizeProvider = [](const std::string& url) -> std::pair<float, float> {
                auto it = g_imageCache.find(url);
                if (it != g_imageCache.end() && it->second) {
                    return {static_cast<float>(it->second->width()), static_cast<float>(it->second->height())};
                }
                return {0.0f, 0.0f};
            };

            auto layout = mdviewer::LayoutEngine::ComputeLayout(docModel, width, g_typeface.get(), imageSizeProvider);

            g_appState.SetFile(path, std::move(*content), std::move(docModel), std::move(layout), pushHistory);

            std::wstring title = L"Markdown Viewer - " + path.filename().wstring();
            SetWindowTextW(hwnd, title.c_str());

            InvalidateRect(hwnd, NULL, FALSE);
            } else {
            MessageBoxW(hwnd, (L"Could not load file: " + path.wstring()).c_str(), L"Error", MB_ICONERROR);
            }
            }

            void OnGoBack(HWND hwnd) {
            std::filesystem::path target;
            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.CanGoBack()) {
                g_appState.historyIndex--;
                target = g_appState.history[g_appState.historyIndex];
            }
            }
            if (!target.empty()) {
            LoadFile(hwnd, target, false);
            }
            }

            void OnGoForward(HWND hwnd) {
            std::filesystem::path target;
            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.CanGoForward()) {
                g_appState.historyIndex++;
                target = g_appState.history[g_appState.historyIndex];
            }
            }
            if (!target.empty()) {
            LoadFile(hwnd, target, false);
            }
            }

            void HandleLinkClick(HWND hwnd, const std::string& url) {
            if (url.empty()) return;

            // 1. Separate fragment/query from the path
            std::string pathPart = url;
            std::string fragment;
            size_t hashPos = pathPart.find('#');
            if (hashPos != std::string::npos) {
            fragment = pathPart.substr(hashPos);
            pathPart = pathPart.substr(0, hashPos);
            }
            size_t queryPos = pathPart.find('?');
            if (queryPos != std::string::npos) {
            pathPart = pathPart.substr(0, queryPos);
            }

            // 2. Simple URL decoding for local paths (e.g., %20 to space)
            auto UrlDecode = [](const std::string& str) {
            std::string res;
            for (size_t i = 0; i < str.length(); ++i) {
                if (str[i] == '%' && i + 2 < str.length()) {
                    int value = 0;
                    std::stringstream ss;
                    ss << std::hex << str.substr(i + 1, 2);
                    ss >> value;
                    res += static_cast<char>(value);
                    i += 2;
                } else if (str[i] == '+') {
                    res += ' ';
                } else {
                    res += str[i];
                }
            }
            return res;
            };

            std::string decodedPath = UrlDecode(pathPart);

            if (url.find("http://") == 0 || url.find("https://") == 0) {
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            } else if (url.find("file://") == 0) {
            std::string localPath = url.substr(7);
            // Remove leading slash if it's like file:///C:/...
            if (localPath.size() > 1 && localPath[0] == '/' && localPath[2] == ':') {
                localPath = localPath.substr(1);
            }
            localPath = UrlDecode(localPath);
            // Strip fragment from file:// too if present
            size_t fHash = localPath.find('#');
            if (fHash != std::string::npos) localPath = localPath.substr(0, fHash);

            std::filesystem::path targetPath(localPath);
            std::string ext = targetPath.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

            if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
                LoadFile(hwnd, targetPath);
            } else {
                ShellExecuteW(NULL, L"open", targetPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            } else {
            std::filesystem::path currentDir = g_appState.currentFilePath.parent_path();
            std::filesystem::path targetPath = currentDir / decodedPath;

            if (!std::filesystem::exists(targetPath)) {
                // Try absolute path
                targetPath = std::filesystem::path(decodedPath);
            }

            if (std::filesystem::exists(targetPath)) {
                std::string ext = targetPath.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

                if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
                    LoadFile(hwnd, targetPath);
                } else {
                    ShellExecuteW(NULL, L"open", targetPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
            }
            }
            }

            void RelayoutCurrentDocument(HWND hwnd) {
            if (!EnsureFontSystem()) {
            return;
            }

            mdviewer::DocumentModel docModel;
            std::filesystem::path currentPath;
            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.sourceText.empty()) {
                return;
            }
            docModel = g_appState.docModel;
            currentPath = g_appState.currentFilePath;
            }

            // Pre-load images just in case (though they should be in cache)
            PreloadImages(docModel, currentPath.parent_path());

            RECT rect;
            GetClientRect(hwnd, &rect);
            const float width = static_cast<float>(rect.right - rect.left);

            auto imageSizeProvider = [](const std::string& url) -> std::pair<float, float> {
            auto it = g_imageCache.find(url);
            if (it != g_imageCache.end() && it->second) {
                return {static_cast<float>(it->second->width()), static_cast<float>(it->second->height())};
            }
            return {0.0f, 0.0f};
            };

            auto layout = mdviewer::LayoutEngine::ComputeLayout(docModel, width, g_typeface.get(), imageSizeProvider);

            {
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.docLayout = std::move(layout);
            ClampScrollOffset(hwnd);
            g_appState.needsRepaint = true;
            }
            }
            }

            void ApplySelectedFont(HWND hwnd, const std::wstring& familyName) {    const std::wstring previousFamily = g_selectedFontFamily;
    g_selectedFontFamily = familyName;
    ResetResolvedTypefaces();

    if (!EnsureFontSystem()) {
        g_selectedFontFamily = previousFamily;
        ResetResolvedTypefaces();
        EnsureFontSystem();
        MessageBoxW(hwnd, L"The selected font could not be loaded.", L"Error", MB_ICONERROR);
        return;
    }

    RelayoutCurrentDocument(hwnd);
    UpdateMenuState(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

void ApplyTheme(HWND hwnd, mdviewer::ThemeMode theme) {
    {
        std::lock_guard<std::mutex> lock(g_appState.mtx);
        g_appState.theme = theme;
        g_appState.needsRepaint = true;
    }
    UpdateMenuState(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

void OnDropFiles(HWND hwnd, HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
    if (count > 0) {
        WCHAR path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH)) {
            LoadFile(hwnd, path);
        }
    }
    DragFinish(hDrop);
}

void UpdateSelectionFromPoint(HWND hwnd, int x, int y) {
    auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));
    if (hit.valid) {
        g_appState.selectionFocus = hit.position;
    }

    const float contentY = static_cast<float>(y) - GetContentTopInset();
    const float viewportHeight = GetViewportHeight(hwnd);
    if (contentY < 0) {
        g_appState.scrollOffset = std::max(g_appState.scrollOffset + contentY, 0.0f);
    } else if (contentY > viewportHeight) {
        g_appState.scrollOffset = std::min(g_appState.scrollOffset + (contentY - viewportHeight), GetMaxScroll(hwnd));
    }
}

void UpdateScrollOffsetFromThumb(HWND hwnd, int mouseY) {
    const auto thumb = GetScrollbarThumbRect(hwnd);
    if (!thumb) {
        return;
    }

    const float viewportHeight = GetViewportHeight(hwnd);
    const float thumbHeight = thumb->height();
    const float trackHeight = viewportHeight - thumbHeight - (kScrollbarMargin * 2.0f);
    const float thumbTop = std::clamp(
        static_cast<float>(mouseY) - g_appState.scrollbarDragOffset,
        kScrollbarMargin,
        kScrollbarMargin + std::max(trackHeight, 0.0f));

    const float maxScroll = GetMaxScroll(hwnd);
    if (maxScroll <= 0.0f) {
        g_appState.scrollOffset = 0.0f;
        return;
    }

    const float normalized = trackHeight > 0.0f ? (thumbTop - kScrollbarMargin) / trackHeight : 0.0f;
    g_appState.scrollOffset = normalized * maxScroll;
}

bool CopySelection(HWND hwnd) {
    if (!g_appState.HasSelection()) {
        return false;
    }

    const size_t selectionStart = GetSelectionStart();
    const size_t selectionEnd = GetSelectionEnd();
    if (selectionEnd > g_appState.docLayout.plainText.size() || selectionStart >= selectionEnd) {
        return false;
    }

    return CopyTextToClipboard(hwnd, g_appState.docLayout.plainText.substr(selectionStart, selectionEnd - selectionStart));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            UpdateSurface(hwnd);
            UpdateMenuState(hwnd);
            return 0;
        case WM_DESTROY:
            StopAutoScroll(hwnd);
            CleanupMenuThemeResources();
            PostQuitMessage(0);
            return 0;
        case WM_PAINT:
            Render(hwnd);
            return 0;
        case WM_MEASUREITEM:
            if (HandleMeasureMenuItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (HandleDrawMenuItem(reinterpret_cast<DRAWITEMSTRUCT*>(lParam))) {
                return TRUE;
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_DROPFILES:
            OnDropFiles(hwnd, (HDROP)wParam);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case kCommandOpenFile:
                    if (const auto path = ShowOpenFileDialog(hwnd)) {
                        LoadFile(hwnd, *path);
                    }
                    return 0;
                case kCommandExit:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case kCommandSelectFont:
                    if (const auto familyName = ShowFontDialog(hwnd)) {
                        ApplySelectedFont(hwnd, *familyName);
                    }
                    return 0;
                case kCommandUseDefaultFont:
                    if (!g_selectedFontFamily.empty()) {
                        ApplySelectedFont(hwnd, L"");
                    }
                    return 0;
                case kCommandThemeLight:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Light);
                    return 0;
                case kCommandThemeSepia:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Sepia);
                    return 0;
                case kCommandThemeDark:
                    ApplyTheme(hwnd, mdviewer::ThemeMode::Dark);
                    return 0;
                case kCommandGoBack:
                    OnGoBack(hwnd);
                    return 0;
                case kCommandGoForward:
                    OnGoForward(hwnd);
                    return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int topMenuIndex = HitTestTopMenu(hwnd, x, y);
            if (topMenuIndex != -1) {
                StopAutoScroll(hwnd);
                SetFocus(hwnd);
                OpenTopMenu(hwnd, topMenuIndex);
                return 0;
            }

            auto hit = HitTestText(static_cast<float>(x), static_cast<float>(y));
            if (hit.valid && !hit.url.empty()) {
                HandleLinkClick(hwnd, hit.url);
                return 0;
            }

            StopAutoScroll(hwnd);
            SetFocus(hwnd);
            SetCapture(hwnd);
            if (const auto thumb = GetScrollbarThumbRect(hwnd); thumb && thumb->contains(static_cast<float>(x), static_cast<float>(y))) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = static_cast<float>(y) - thumb->top();
            } else if (x >= g_surface->width() - static_cast<int>(kScrollbarWidth + (kScrollbarMargin * 2.0f)) && GetScrollbarThumbRect(hwnd)) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isDraggingScrollbar = true;
                g_appState.isSelecting = false;
                g_appState.scrollbarDragOffset = GetScrollbarThumbRect(hwnd)->height() * 0.5f;
                UpdateScrollOffsetFromThumb(hwnd, y);
            } else {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.isSelecting = true;
                g_appState.isDraggingScrollbar = false;
                if (hit.valid) {
                    g_appState.selectionAnchor = hit.position;
                    g_appState.selectionFocus = hit.position;
                } else {
                    g_appState.selectionAnchor = 0;
                    g_appState.selectionFocus = 0;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_XBUTTONDOWN:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
                OnGoBack(hwnd);
            } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
                OnGoForward(hwnd);
            }
            return TRUE;
        case WM_MOUSEMOVE: {
            if (GET_Y_LPARAM(lParam) < kMenuBarHeight) {
                const int hoveredIndex = HitTestTopMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                if (hoveredIndex != g_hoveredTopMenuIndex) {
                    g_hoveredTopMenuIndex = hoveredIndex;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (g_hoveredTopMenuIndex != -1 && g_activeTopMenuIndex == -1) {
                g_hoveredTopMenuIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            bool isAutoScrolling = false;
            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                isAutoScrolling = g_appState.isAutoScrolling;
                if (isAutoScrolling) {
                    g_appState.autoScrollCursorX = static_cast<float>(GET_X_LPARAM(lParam));
                    g_appState.autoScrollCursorY = static_cast<float>(GET_Y_LPARAM(lParam));
                }
            }
            if (isAutoScrolling) {
                TickAutoScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if ((wParam & MK_LBUTTON) == 0) {
                return 0;
            }

            std::lock_guard<std::mutex> lock(g_appState.mtx);
            if (g_appState.isDraggingScrollbar) {
                UpdateScrollOffsetFromThumb(hwnd, GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (g_appState.isSelecting) {
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                ClampScrollOffset(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            ReleaseCapture();
            std::lock_guard<std::mutex> lock(g_appState.mtx);
            g_appState.isDraggingScrollbar = false;
            if (g_appState.isSelecting) {
                UpdateSelectionFromPoint(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            g_appState.isSelecting = false;
            if (!g_appState.HasSelection()) {
                g_appState.selectionAnchor = 0;
                g_appState.selectionFocus = 0;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MBUTTONDOWN:
            if (GET_Y_LPARAM(lParam) < kMenuBarHeight) {
                return 0;
            }
            SetFocus(hwnd);
            if (g_appState.isAutoScrolling) {
                StopAutoScroll(hwnd);
            } else {
                StartAutoScroll(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_MBUTTONUP:
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                g_appState.scrollOffset -= static_cast<float>(delta);
                ClampScrollOffset(hwnd);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                StopAutoScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (GetKeyState(VK_MENU) & 0x8000) {
                if (wParam == VK_LEFT) {
                    OnGoBack(hwnd);
                    return 0;
                }
                if (wParam == VK_RIGHT) {
                    OnGoForward(hwnd);
                    return 0;
                }
            }
            if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
                std::lock_guard<std::mutex> lock(g_appState.mtx);
                CopySelection(hwnd);
                return 0;
            }
            break;
        case WM_TIMER:
            if (wParam == kAutoScrollTimerId) {
                if (TickAutoScroll(hwnd)) {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd) {
                StopAutoScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case WM_SIZE: {
            UpdateSurface(hwnd);
            RelayoutCurrentDocument(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HRESULT comInitResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitializeCom = SUCCEEDED(comInitResult);
    if (FAILED(comInitResult) && comInitResult != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"COM initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        return 1;
    }

    const WCHAR CLASS_NAME[] = L"MDViewerWindowClass";

    auto* largeIcon = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    auto* smallIcon = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(kAppIconResourceId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = largeIcon;
    wc.hIconSm = smallIcon ? smallIcon : largeIcon;

    RegisterClassExW(&wc);

    if (!CreateMenus()) {
        MessageBoxW(nullptr, L"Menu initialization failed. The application cannot start.", L"Error", MB_ICONERROR);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Markdown Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kInitialWindowWidth, kInitialWindowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        CleanupMenuThemeResources();
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 0;
    }

    if (!EnsureFontSystem()) {
        MessageBoxW(hwnd, L"Font initialization failed. The application cannot render text.", L"Error", MB_ICONERROR);
        DestroyWindow(hwnd);
        if (shouldUninitializeCom) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            LoadFile(hwnd, argv[1]);
        }
        LocalFree(argv);
    }

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (shouldUninitializeCom) {
        CoUninitialize();
    }

    return 0;
}
