#include "render/menu_renderer.h"

#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkFontMetrics.h"

namespace mdviewer {

constexpr float kMenuBarHorizontalPadding = 12.0f;
constexpr float kMenuBarItemGap = 8.0f;
constexpr float kMenuTextPadding = 10.0f;
constexpr float kTopMenuFontSize = 17.5f;

std::vector<float> MeasureMenuBarItemWidths(const std::vector<MenuBarItem>& items, SkTypeface* typeface) {
    std::vector<float> widths;
    widths.reserve(items.size());

    if (!typeface) {
        widths.resize(items.size(), 0.0f);
        return widths;
    }

    SkFont menuFont(sk_ref_sp(typeface), kTopMenuFontSize);
    menuFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    menuFont.setSubpixel(true);

    for (const auto& item : items) {
        SkRect textBounds;
        menuFont.measureText(item.label.c_str(), item.label.size(), SkTextEncoding::kUTF8, &textBounds);
        widths.push_back(textBounds.width());
    }

    return widths;
}

MenuBarLayout ComputeMenuBarLayout(float width, float height, const std::vector<float>& itemWidths) {
    MenuBarLayout layout;
    layout.bounds = SkRect::MakeXYWH(0.0f, 0.0f, width, height);

    float currentX = kMenuBarHorizontalPadding;
    for (float textWidth : itemWidths) {
        const float itemWidth = textWidth + kMenuTextPadding * 2.0f;
        layout.itemRects.push_back(SkRect::MakeXYWH(currentX, 0.0f, itemWidth, height));
        currentX += itemWidth + kMenuBarItemGap;
    }

    const float btnSize = 34.0f;
    const float gap = 4.0f;
    const float btnY = (height - btnSize) * 0.5f;
    float rightX = width - kMenuBarHorizontalPadding - btnSize;

    layout.forwardRect = SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize);
    rightX -= (btnSize + gap);
    layout.backRect = SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize);
    rightX -= (btnSize + gap);
    layout.zoomInRect = SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize);
    rightX -= (btnSize + gap);
    layout.zoomOutRect = SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize);

    return layout;
}

int HitTestMenuBarLayout(const MenuBarLayout& layout, float x, float y) {
    if (!layout.bounds.contains(x, y)) {
        return -1;
    }

    for (size_t index = 0; index < layout.itemRects.size(); ++index) {
        if (layout.itemRects[index].contains(x, y)) {
            return static_cast<int>(index);
        }
    }

    if (layout.zoomOutRect.contains(x, y)) return -4;
    if (layout.zoomInRect.contains(x, y)) return -5;
    if (layout.backRect.contains(x, y)) return -2;
    if (layout.forwardRect.contains(x, y)) return -3;
    return -1;
}

float MeasureDropdownWidth(const std::vector<DropdownItem>& items, SkTypeface* typeface) {
    if (!typeface || items.empty()) {
        return 150.0f;
    }

    SkFont menuFont(sk_ref_sp(typeface), kTopMenuFontSize);
    menuFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    menuFont.setSubpixel(true);

    float maxWidth = 150.0f;
    for (const auto& item : items) {
        if (item.isSeparator) {
            continue;
        }
        SkRect bounds;
        menuFont.measureText(item.label.c_str(), item.label.size(), SkTextEncoding::kUTF8, &bounds);
        maxWidth = std::max(maxWidth, bounds.width() + 40.0f);
    }

    return maxWidth;
}

void DrawMenuBar(
    SkCanvas& canvas,
    float width,
    float height,
    const std::vector<MenuBarItem>& items,
    const MenuBarState& state,
    SkTypeface* typeface,
    const ThemePalette& palette) {
    
    SkPaint backgroundPaint;
    backgroundPaint.setColor(palette.menuBackground);
    canvas.drawRect(SkRect::MakeXYWH(0, 0, width, height), backgroundPaint);

    if (!typeface) return;

    SkFont menuFont(sk_ref_sp(typeface), kTopMenuFontSize);
    menuFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    menuFont.setSubpixel(true);

    SkFontMetrics metrics;
    menuFont.getMetrics(&metrics);
    const float baselineY = std::round((height - (metrics.fDescent - metrics.fAscent)) * 0.5f - metrics.fAscent);

    const auto layout = ComputeMenuBarLayout(width, height, MeasureMenuBarItemWidths(items, typeface));
    SkPaint textPaint;
    textPaint.setAntiAlias(true);

    for (size_t i = 0; i < items.size(); ++i) {
        const bool isHighlighted = (static_cast<int>(i) == state.hoveredIndex || static_cast<int>(i) == state.activeIndex);
        const SkRect& itemRect = layout.itemRects[i];

        if (isHighlighted) {
            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(palette.menuSelectedBackground);
            canvas.drawRoundRect(
                SkRect::MakeXYWH(itemRect.x(), 4.0f, itemRect.width(), height - 8.0f),
                6.0f,
                6.0f,
                highlightPaint);
        }

        textPaint.setColor(isHighlighted ? palette.menuSelectedText : palette.menuText);
        canvas.drawSimpleText(
            items[i].label.c_str(),
            items[i].label.size(),
            SkTextEncoding::kUTF8,
            itemRect.x() + kMenuTextPadding,
            baselineY,
            menuFont,
            textPaint);
    }

    auto drawBtn = [&](const SkRect& rect, bool enabled, int hoverId, auto&& glyph) {
        const bool isHovered = (state.hoveredIndex == hoverId);
        if (enabled && isHovered) {
            SkPaint hp;
            hp.setAntiAlias(true);
            hp.setColor(palette.menuSelectedBackground);
            canvas.drawRoundRect(rect, 6.0f, 6.0f, hp);
        }
        SkColor c = enabled ? (isHovered ? palette.menuSelectedText : palette.menuText) : palette.menuDisabledText;
        glyph(c, rect.centerX(), rect.centerY());
    };

    drawBtn(layout.forwardRect, state.canGoForward, -3, [&](SkColor c, float cx, float cy) { DrawArrow(canvas, cx, cy, 14.0f, true, c); });
    drawBtn(layout.backRect, state.canGoBack, -2, [&](SkColor c, float cx, float cy) { DrawArrow(canvas, cx, cy, 14.0f, false, c); });
    drawBtn(layout.zoomInRect, state.canZoomIn, -5, [&](SkColor c, float cx, float cy) { DrawZoomGlyph(canvas, cx, cy, 12.0f, true, c); });
    drawBtn(layout.zoomOutRect, state.canZoomOut, -4, [&](SkColor c, float cx, float cy) { DrawZoomGlyph(canvas, cx, cy, 12.0f, false, c); });
}

void DrawDropdown(
    SkCanvas& canvas,
    float x,
    float y,
    const std::vector<DropdownItem>& items,
    int hoveredItemIndex,
    SkTypeface* typeface,
    const ThemePalette& palette) {
    
    if (!typeface || items.empty()) return;

    SkFont menuFont(sk_ref_sp(typeface), kTopMenuFontSize);
    menuFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    menuFont.setSubpixel(true);

    const float itemHeight = 30.0f;
    const float maxWidth = MeasureDropdownWidth(items, typeface);
    const float dropdownHeight = items.size() * itemHeight;

    SkPaint bgPaint;
    bgPaint.setColor(palette.menuBackground);
    bgPaint.setAntiAlias(true);
    
    SkRect dropdownRect = SkRect::MakeXYWH(x, y, maxWidth, dropdownHeight);
    canvas.drawRect(dropdownRect, bgPaint);

    SkPaint borderPaint;
    borderPaint.setColor(palette.menuSeparator);
    borderPaint.setStyle(SkPaint::kStroke_Style);
    canvas.drawRect(dropdownRect, borderPaint);

    SkFontMetrics metrics;
    menuFont.getMetrics(&metrics);
    const float textCenterOff = (itemHeight - (metrics.fDescent - metrics.fAscent)) * 0.5f - metrics.fAscent;

    for (size_t i = 0; i < items.size(); ++i) {
        float itemY = y + i * itemHeight;
        if (items[i].isSeparator) {
            SkPaint p;
            p.setColor(palette.menuSeparator);
            canvas.drawLine(x + 5, itemY + itemHeight/2, x + maxWidth - 5, itemY + itemHeight/2, p);
            continue;
        }

        const bool isHighlighted = (static_cast<int>(i) == hoveredItemIndex);
        if (isHighlighted) {
            SkPaint hp;
            hp.setColor(palette.menuSelectedBackground);
            canvas.drawRect(SkRect::MakeXYWH(x + 2, itemY + 2, maxWidth - 4, itemHeight - 4), hp);
        }

        SkPaint tp;
        tp.setAntiAlias(true);
        tp.setColor(isHighlighted ? palette.menuSelectedText : palette.menuText);
        canvas.drawSimpleText(items[i].label.c_str(), items[i].label.size(), SkTextEncoding::kUTF8,
                             x + 10, itemY + textCenterOff, menuFont, tp);
    }
}

void DrawArrow(SkCanvas& canvas, float cx, float cy, float size, bool pointingRight, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(2.0f);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);

    const float halfSize = size * 0.5f;
    const float tipX = pointingRight ? cx + halfSize : cx - halfSize;
    const float tailX = pointingRight ? cx - halfSize : cx + halfSize;

    SkPathBuilder builder;
    builder.moveTo(tailX, cy);
    builder.lineTo(tipX, cy);
    builder.moveTo(tipX - (pointingRight ? 5.0f : -5.0f), cy - 5.0f);
    builder.lineTo(tipX, cy);
    builder.lineTo(tipX - (pointingRight ? 5.0f : -5.0f), cy + 5.0f);
    canvas.drawPath(builder.detach(), paint);
}

void DrawZoomGlyph(SkCanvas& canvas, float cx, float cy, float size, bool isPlus, SkColor color) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(2.0f);
    paint.setStrokeCap(SkPaint::kRound_Cap);

    const float hs = size * 0.5f;
    canvas.drawLine(cx - hs, cy, cx + hs, cy, paint);
    if (isPlus) {
        canvas.drawLine(cx, cy - hs, cx, cy + hs, paint);
    }
}

} // namespace mdviewer
