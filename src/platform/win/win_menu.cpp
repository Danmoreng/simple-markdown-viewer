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

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
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
constexpr float kTopMenuFontSize = 17.5f;

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
HBRUSH g_menuBackgroundBrush = nullptr;
HFONT g_menuFont = nullptr;
bool g_ownsMenuFont = false;
int g_hoveredTopMenuIndex = -1;
int g_activeTopMenuIndex = -1;
std::list<MenuItemData> g_menuItemData;

std::array<TopMenuItem, 2> GetTopMenuItems() {
    return {{
        {L"File", g_fileMenu},
        {L"View", g_viewMenu},
    }};
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

void DrawZoomGlyph(SkCanvas* canvas, float x, float y, float size, bool plus, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(2.0f);
    paint.setStrokeCap(SkPaint::kRound_Cap);

    const float halfSize = size * 0.5f;
    canvas->drawLine(x - halfSize, y, x + halfSize, y, paint);
    if (plus) {
        canvas->drawLine(x, y - halfSize, x, y + halfSize, paint);
    }
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

void UpdateMenuState(HWND hwnd, ThemeMode currentTheme, bool hasCustomFont, const ThemePalette& palette) {
    if (!g_viewMenu || !g_themeMenu) {
        return;
    }

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

int HitTestTopMenu(HWND hwnd, int x, int y, int surfaceWidth) {
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

    const float rightEdge = static_cast<float>(surfaceWidth) - 12.0f;
    const float btnSize = 34.0f;
    const float gap = 4.0f;
    const float forwardBtnX = rightEdge - btnSize;
    const float backBtnX = forwardBtnX - btnSize - gap;
    const float zoomInBtnX = backBtnX - btnSize - gap;
    const float zoomOutBtnX = zoomInBtnX - btnSize - gap;
    const float btnY = (static_cast<float>(kMenuBarHeight) - btnSize) * 0.5f;

    if (y >= btnY && y < btnY + btnSize) {
        if (x >= zoomOutBtnX && x < zoomOutBtnX + btnSize) return -4;
        if (x >= zoomInBtnX && x < zoomInBtnX + btnSize) return -5;
        if (x >= backBtnX && x < backBtnX + btnSize) return -2;
        if (x >= forwardBtnX && x < forwardBtnX + btnSize) return -3;
    }

    return -1;
}

bool UpdateTopMenuHover(HWND hwnd, int x, int y, int surfaceWidth) {
    int hoveredIndex = -1;
    if (y < kMenuBarHeight) {
        hoveredIndex = HitTestTopMenu(hwnd, x, y, surfaceWidth);
    } else if (g_activeTopMenuIndex != -1) {
        return false;
    }

    if (hoveredIndex == g_hoveredTopMenuIndex) {
        return false;
    }

    g_hoveredTopMenuIndex = hoveredIndex;
    return true;
}

UINT OpenTopMenu(HWND hwnd, int menuIndex) {
    switch (menuIndex) {
        case -2:
            return kCommandGoBack;
        case -3:
            return kCommandGoForward;
        case -4:
            return kCommandZoomOut;
        case -5:
            return kCommandZoomIn;
        default:
            break;
    }

    const auto items = GetTopMenuItems();
    const auto rects = GetTopMenuItemRects(hwnd);
    if (menuIndex < 0 || static_cast<size_t>(menuIndex) >= items.size() || static_cast<size_t>(menuIndex) >= rects.size()) {
        return 0;
    }

    HMENU menu = items[menuIndex].menu;
    if (!menu) {
        return 0;
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
    SkPaint backgroundPaint;
    backgroundPaint.setAntiAlias(false);
    backgroundPaint.setColor(palette.menuBackground);
    canvas->drawRect(
        SkRect::MakeXYWH(0.0f, 0.0f, static_cast<float>(surfaceWidth), static_cast<float>(kMenuBarHeight)),
        backgroundPaint);

    if (!typeface) {
        return;
    }

    SkFont menuFont;
    menuFont.setTypeface(sk_ref_sp(typeface));
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

    const float rightEdge = static_cast<float>(surfaceWidth) - 12.0f;
    const float btnSize = 34.0f;
    const float gap = 4.0f;
    const float forwardBtnX = rightEdge - btnSize;
    const float backBtnX = forwardBtnX - btnSize - gap;
    const float zoomInBtnX = backBtnX - btnSize - gap;
    const float zoomOutBtnX = zoomInBtnX - btnSize - gap;
    const float btnY = (static_cast<float>(kMenuBarHeight) - btnSize) * 0.5f;

    auto drawToolbarButton = [&](float x, float y, bool enabled, int hoverIndex, auto&& glyphDrawer) {
        const bool isHovered = g_hoveredTopMenuIndex == hoverIndex;
        if (enabled && isHovered) {
            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(palette.menuSelectedBackground);
            canvas->drawRoundRect(SkRect::MakeXYWH(x, y, btnSize, btnSize), 6.0f, 6.0f, highlightPaint);
        }
        const SkColor color = enabled ? (isHovered ? palette.menuSelectedText : palette.menuText) : palette.menuDisabledText;
        glyphDrawer(color, x, y);
    };

    drawToolbarButton(zoomOutBtnX, btnY, canZoomOut, -4, [&](SkColor color, float x, float y) {
        DrawZoomGlyph(canvas, x + btnSize * 0.5f, y + btnSize * 0.5f, 12.0f, false, color);
    });
    drawToolbarButton(zoomInBtnX, btnY, canZoomIn, -5, [&](SkColor color, float x, float y) {
        DrawZoomGlyph(canvas, x + btnSize * 0.5f, y + btnSize * 0.5f, 12.0f, true, color);
    });
    drawToolbarButton(backBtnX, btnY, canGoBack, -2, [&](SkColor color, float x, float y) {
        DrawArrow(canvas, x + btnSize * 0.5f, y + btnSize * 0.5f, 14.0f, false, color);
    });
    drawToolbarButton(forwardBtnX, btnY, canGoForward, -3, [&](SkColor color, float x, float y) {
        DrawArrow(canvas, x + btnSize * 0.5f, y + btnSize * 0.5f, 14.0f, true, color);
    });
}

} // namespace mdviewer::win
