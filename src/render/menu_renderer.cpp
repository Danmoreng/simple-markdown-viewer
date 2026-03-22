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

    float currentX = kMenuBarHorizontalPadding;
    SkPaint textPaint;
    textPaint.setAntiAlias(true);

    for (size_t i = 0; i < items.size(); ++i) {
        const bool isHighlighted = (static_cast<int>(i) == state.hoveredIndex || static_cast<int>(i) == state.activeIndex);
        
        SkRect textBounds;
        menuFont.measureText(items[i].label.c_str(), items[i].label.size(), SkTextEncoding::kUTF8, &textBounds);
        
        const float itemWidth = textBounds.width() + kMenuTextPadding * 2.0f;

        if (isHighlighted) {
            SkPaint highlightPaint;
            highlightPaint.setAntiAlias(true);
            highlightPaint.setColor(palette.menuSelectedBackground);
            canvas.drawRoundRect(SkRect::MakeXYWH(currentX, 4.0f, itemWidth, height - 8.0f), 6.0f, 6.0f, highlightPaint);
        }

        textPaint.setColor(isHighlighted ? palette.menuSelectedText : palette.menuText);
        canvas.drawSimpleText(items[i].label.c_str(), items[i].label.size(), SkTextEncoding::kUTF8, 
                             currentX + kMenuTextPadding, baselineY, menuFont, textPaint);
        
        currentX += itemWidth + kMenuBarItemGap;
    }

    // Draw Toolbar Buttons (Right side)
    const float btnSize = 34.0f;
    const float gap = 4.0f;
    const float btnY = (height - btnSize) * 0.5f;
    float rightX = width - kMenuBarHorizontalPadding - btnSize;

    auto drawBtn = [&](bool enabled, int hoverId, auto&& glyph) {
        const bool isHovered = (state.hoveredIndex == hoverId);
        if (enabled && isHovered) {
            SkPaint hp;
            hp.setAntiAlias(true);
            hp.setColor(palette.menuSelectedBackground);
            canvas.drawRoundRect(SkRect::MakeXYWH(rightX, btnY, btnSize, btnSize), 6.0f, 6.0f, hp);
        }
        SkColor c = enabled ? (isHovered ? palette.menuSelectedText : palette.menuText) : palette.menuDisabledText;
        glyph(c, rightX + btnSize * 0.5f, btnY + btnSize * 0.5f);
        rightX -= (btnSize + gap);
    };

    drawBtn(state.canGoForward, -3, [&](SkColor c, float cx, float cy) { DrawArrow(canvas, cx, cy, 14.0f, true, c); });
    drawBtn(state.canGoBack, -2, [&](SkColor c, float cx, float cy) { DrawArrow(canvas, cx, cy, 14.0f, false, c); });
    drawBtn(state.canZoomIn, -5, [&](SkColor c, float cx, float cy) { DrawZoomGlyph(canvas, cx, cy, 12.0f, true, c); });
    drawBtn(state.canZoomOut, -4, [&](SkColor c, float cx, float cy) { DrawZoomGlyph(canvas, cx, cy, 12.0f, false, c); });
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

    float maxWidth = 150.0f;
    for (const auto& item : items) {
        if (item.isSeparator) continue;
        SkRect bounds;
        menuFont.measureText(item.label.c_str(), item.label.size(), SkTextEncoding::kUTF8, &bounds);
        maxWidth = std::max(maxWidth, bounds.width() + 40.0f);
    }

    const float itemHeight = 30.0f;
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
