#include "platform/win/win_menu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "render/menu_renderer.h"

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

namespace mdviewer::win {
namespace {

constexpr int kMenuHorizontalPadding = 12;
constexpr int kMenuVerticalPadding = 6;
constexpr int kMenuCheckGutterWidth = 26;
constexpr int kMenuArrowAreaWidth = 16;
constexpr int kMenuLabelGap = 12;
constexpr int kMenuBarLeftInset = 12;
constexpr int kMenuBarItemGap = 8;
constexpr UINT_PTR kCommandRecentFileBase = 1400;
constexpr size_t kMaxRecentFiles = 8;

struct MenuItemData {
    std::wstring text;
    bool isSeparator = false;
    bool hasSubmenu = false;
};

struct TopMenuItem {
    const wchar_t* label;
    HMENU menu;
};

HMENU g_fileMenu = nullptr;
HMENU g_viewMenu = nullptr;
HMENU g_themeMenu = nullptr;
std::vector<std::filesystem::path> g_recentFiles;
HBRUSH g_menuBackgroundBrush = nullptr;
HFONT g_menuFont = nullptr;
bool g_ownsMenuFont = false;
MenuBarState g_menuBarState;
std::optional<MenuBarLayout> g_lastMenuBarLayout;
std::list<MenuItemData> g_menuItemData;

std::array<TopMenuItem, 2> GetTopMenuItems() {
    return {{
        {L"File", g_fileMenu},
        {L"View", g_viewMenu},
    }};
}

HFONT GetMenuFontHandle();

const std::vector<MenuBarItem>& GetTopMenuBarItems() {
    static const std::vector<MenuBarItem> items = {
        {"File", 0},
        {"View", 1},
    };
    return items;
}

std::vector<float> MeasureTopMenuItemWidths(HWND hwnd) {
    std::vector<float> widths;
    widths.reserve(GetTopMenuItems().size());

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        widths.resize(GetTopMenuItems().size(), 0.0f);
        return widths;
    }

    HGDIOBJ oldFont = SelectObject(hdc, GetMenuFontHandle());
    for (const auto& item : GetTopMenuItems()) {
        SIZE size = {};
        GetTextExtentPoint32W(hdc, item.label, static_cast<int>(wcslen(item.label)), &size);
        widths.push_back(static_cast<float>(size.cx));
    }
    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);
    return widths;
}

MenuBarLayout GetTopMenuBarLayout(HWND hwnd, int surfaceWidth) {
    if (g_lastMenuBarLayout &&
        std::abs(g_lastMenuBarLayout->bounds.width() - static_cast<float>(surfaceWidth)) < 0.01f) {
        return *g_lastMenuBarLayout;
    }

    return ComputeMenuBarLayout(
        static_cast<float>(surfaceWidth),
        static_cast<float>(kMenuBarHeight),
        MeasureTopMenuItemWidths(hwnd));
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

std::wstring EscapeMenuText(const std::wstring& text) {
    std::wstring escaped;
    escaped.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'&') {
            escaped.push_back(L'&');
        }
        escaped.push_back(ch);
    }
    return escaped;
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

        MENUITEMINFOW writeInfo = {};
        writeInfo.cbSize = sizeof(writeInfo);
        writeInfo.fMask = MIIM_FTYPE | MIIM_DATA;
        writeInfo.fType = itemData.isSeparator ? (MFT_SEPARATOR | MFT_OWNERDRAW) : MFT_OWNERDRAW;
        writeInfo.dwItemData = reinterpret_cast<ULONG_PTR>(&itemData);
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

void RefreshMenuThemeResources(HWND hwnd, const ThemePalette& palette) {
    if (g_menuBackgroundBrush) {
        DeleteObject(g_menuBackgroundBrush);
        g_menuBackgroundBrush = nullptr;
    }

    g_menuBackgroundBrush = CreateSolidBrush(ToColorRef(palette.menuBackground));
    ApplyMenuBackgroundBrushRecursive(g_fileMenu);
    ApplyMenuBackgroundBrushRecursive(g_viewMenu);
    if (hwnd) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

std::vector<RECT> GetTopMenuItemRects(HWND hwnd) {
    std::vector<RECT> rects;
    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);
    const auto layout = GetTopMenuBarLayout(hwnd, clientRect.right - clientRect.left);

    for (const auto& itemRect : layout.itemRects) {
        RECT rect = {
            static_cast<LONG>(std::lround(itemRect.left())),
            static_cast<LONG>(std::lround(itemRect.top())),
            static_cast<LONG>(std::lround(itemRect.right())),
            static_cast<LONG>(std::lround(itemRect.bottom()))
        };
        rects.push_back(rect);
    }
    return rects;
}

} // namespace

bool CreateMenus(const ThemePalette& palette) {
    CleanupMenus();

    g_fileMenu = CreatePopupMenu();
    g_viewMenu = CreatePopupMenu();
    g_themeMenu = CreatePopupMenu();
    if (!g_fileMenu || !g_viewMenu || !g_themeMenu) {
        CleanupMenus();
        return false;
    }

    AppendMenuW(g_fileMenu, MF_STRING, kCommandOpenFile, L"&Open...\tCtrl+O");
    AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_fileMenu, MF_STRING, kCommandExit, L"E&xit");

    AppendMenuW(g_viewMenu, MF_STRING, kCommandSelectFont, L"Select &Font...");
    AppendMenuW(g_viewMenu, MF_STRING, kCommandUseDefaultFont, L"Use &Default Font");
    AppendMenuW(g_viewMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_viewMenu, MF_STRING, kCommandFind, L"&Find...\tCtrl+F");
    AppendMenuW(g_viewMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeLight, L"&Light");
    AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeSepia, L"&Sepia");
    AppendMenuW(g_themeMenu, MF_STRING, kCommandThemeDark, L"&Dark");
    AppendMenuW(g_viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(g_themeMenu), L"&Theme");

    ConfigureOwnerDrawMenu(g_fileMenu);
    ConfigureOwnerDrawMenu(g_viewMenu);
    RefreshMenuThemeResources(nullptr, palette);
    return true;
}

void CleanupMenus() {
    g_menuBarState = {};
    g_lastMenuBarLayout.reset();
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

void UpdateMenuState(
    HWND hwnd,
    ThemeMode currentTheme,
    bool hasCustomFont,
    const ThemePalette& palette,
    const std::vector<std::filesystem::path>& recentFiles) {
    if (!g_viewMenu || !g_themeMenu) {
        return;
    }

    g_recentFiles = recentFiles;
    if (g_fileMenu) {
        while (GetMenuItemCount(g_fileMenu) > 0) {
            DeleteMenu(g_fileMenu, 0, MF_BYPOSITION);
        }

        AppendMenuW(g_fileMenu, MF_STRING, kCommandOpenFile, L"&Open...\tCtrl+O");
        if (!g_recentFiles.empty()) {
            AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
            const size_t recentCount = std::min(g_recentFiles.size(), kMaxRecentFiles);
            for (size_t index = 0; index < recentCount; ++index) {
                const std::wstring path = EscapeMenuText(g_recentFiles[index].wstring());
                const std::wstring label = L"&" + std::to_wstring(index + 1) + L" " + path;
                AppendMenuW(g_fileMenu, MF_STRING, kCommandRecentFileBase + index, label.c_str());
            }
        }
        AppendMenuW(g_fileMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(g_fileMenu, MF_STRING, kCommandExit, L"E&xit");
    }
    g_menuItemData.clear();
    ConfigureOwnerDrawMenu(g_fileMenu);
    ConfigureOwnerDrawMenu(g_viewMenu);

    EnableMenuItem(
        g_viewMenu,
        kCommandUseDefaultFont,
        MF_BYCOMMAND | (hasCustomFont ? MF_ENABLED : MF_GRAYED));
    CheckMenuRadioItem(
        g_themeMenu,
        kCommandThemeLight,
        kCommandThemeDark,
        currentTheme == ThemeMode::Sepia ? kCommandThemeSepia :
        (currentTheme == ThemeMode::Dark ? kCommandThemeDark : kCommandThemeLight),
        MF_BYCOMMAND);
    RefreshMenuThemeResources(hwnd, palette);
}

std::optional<std::filesystem::path> GetRecentFileForCommand(UINT_PTR commandId) {
    if (commandId < kCommandRecentFileBase) {
        return std::nullopt;
    }

    const size_t index = static_cast<size_t>(commandId - kCommandRecentFileBase);
    if (index >= g_recentFiles.size() || index >= kMaxRecentFiles) {
        return std::nullopt;
    }

    return g_recentFiles[index];
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

bool HandleDrawMenuItem(const DRAWITEMSTRUCT* drawInfo, const ThemePalette& palette) {
    if (!drawInfo || drawInfo->CtlType != ODT_MENU) {
        return false;
    }

    const auto* itemData = reinterpret_cast<const MenuItemData*>(drawInfo->itemData);
    if (!itemData) {
        return false;
    }

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

        const SIZE accelSize = MeasureMenuSegment(drawInfo->hDC, accelText);
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

MenuBarHitTestResult HitTestTopMenu(HWND hwnd, int x, int y, int surfaceWidth) {
    if (y < 0 || y >= kMenuBarHeight) {
        return {};
    }

    return HitTestMenuBarLayout(GetTopMenuBarLayout(hwnd, surfaceWidth), static_cast<float>(x), static_cast<float>(y));
}

bool UpdateTopMenuHover(HWND hwnd, int x, int y, int surfaceWidth) {
    int hoveredIndex = -1;
    if (y < kMenuBarHeight) {
        hoveredIndex = MenuBarStateIndexFromHit(HitTestTopMenu(hwnd, x, y, surfaceWidth));
    } else if (g_menuBarState.activeIndex != -1) {
        return false;
    }

    if (hoveredIndex == g_menuBarState.hoveredIndex) {
        return false;
    }

    g_menuBarState.hoveredIndex = hoveredIndex;
    return true;
}

UINT OpenTopMenu(HWND hwnd, const MenuBarHitTestResult& hit) {
    switch (hit.target) {
        case MenuBarHitTarget::GoBack:
            return kCommandGoBack;
        case MenuBarHitTarget::GoForward:
            return kCommandGoForward;
        case MenuBarHitTarget::ZoomOut:
            return kCommandZoomOut;
        case MenuBarHitTarget::ZoomIn:
            return kCommandZoomIn;
        case MenuBarHitTarget::MenuItem:
            break;
        case MenuBarHitTarget::None:
        default:
            return 0;
    }

    const int menuIndex = hit.menuIndex;
    const auto items = GetTopMenuItems();
    RECT clientRect = {};
    GetClientRect(hwnd, &clientRect);
    const auto layout = GetTopMenuBarLayout(hwnd, clientRect.right - clientRect.left);
    if (menuIndex < 0 || static_cast<size_t>(menuIndex) >= items.size() || static_cast<size_t>(menuIndex) >= layout.itemRects.size()) {
        return 0;
    }

    HMENU menu = items[menuIndex].menu;
    if (!menu) {
        return 0;
    }

    g_menuBarState.activeIndex = menuIndex;
    InvalidateRect(hwnd, nullptr, FALSE);

    const SkRect& itemRect = layout.itemRects[menuIndex];
    POINT screenPoint = {
        static_cast<LONG>(std::lround(itemRect.left())),
        static_cast<LONG>(std::lround(itemRect.bottom()))
    };
    ClientToScreen(hwnd, &screenPoint);
    const UINT command = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenPoint.x,
        screenPoint.y,
        hwnd,
        nullptr);

    g_menuBarState.activeIndex = -1;
    g_menuBarState.hoveredIndex = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
    return command;
}

void DrawTopMenuBar(
    SkCanvas* canvas,
    HWND hwnd,
    int surfaceWidth,
    SkTypeface* typeface,
    const ThemePalette& palette,
    bool canGoBack,
    bool canGoForward,
    bool canZoomOut,
    bool canZoomIn) {
    (void)hwnd;

    MenuBarState renderState = g_menuBarState;
    renderState.canGoBack = canGoBack;
    renderState.canGoForward = canGoForward;
    renderState.canZoomOut = canZoomOut;
    renderState.canZoomIn = canZoomIn;
    g_lastMenuBarLayout = ComputeMenuBarLayout(
        static_cast<float>(surfaceWidth),
        static_cast<float>(kMenuBarHeight),
        MeasureMenuBarItemWidths(GetTopMenuBarItems(), typeface));

    DrawMenuBar(
        *canvas,
        static_cast<float>(surfaceWidth),
        static_cast<float>(kMenuBarHeight),
        GetTopMenuBarItems(),
        renderState,
        typeface,
        palette);
}

} // namespace mdviewer::win
