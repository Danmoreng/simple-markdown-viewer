#pragma once
#include <cmath>
#include "app/native_badge.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"

namespace mdviewer {
// Draw directly into the destination, so zoom never resamples badge text pixels.
inline void DrawNativeBadge(SkCanvas* canvas, const NativeBadge& badge,
    const sk_sp<SkTypeface>& face, SkRect rect, bool snapToPixels) {
    if (rect.isEmpty()) return;
    SkAutoCanvasRestore restore(canvas, true);
    if (snapToPixels && canvas->getTotalMatrix().isScaleTranslate()) {
        rect = canvas->getTotalMatrix().mapRect(rect);
        rect = SkRect::MakeXYWH(std::round(rect.left()), std::round(rect.top()),
            std::max(1.0f, std::round(rect.width())), std::max(1.0f, std::round(rect.height())));
        canvas->resetMatrix();
    }
    SkFont base(face, 11.0f);
    const float labelWidth = badge.label.empty() ? 0.0f :
        std::ceil(base.measureText(badge.label.data(), badge.label.size(), SkTextEncoding::kUTF8)) + 10.0f;
    const float messageWidth = std::ceil(base.measureText(badge.message.data(), badge.message.size(), SkTextEncoding::kUTF8)) + 10.0f;
    const float scaleX = rect.width() / (labelWidth + messageWidth);
    const float scaleY = rect.height() / 20.0f;
    const float split = snapToPixels ? std::round(rect.left() + labelWidth * scaleX) : rect.left() + labelWidth * scaleX;
    canvas->clipRRect(SkRRect::MakeRectXY(rect, badge.square ? 0.0f : 3.0f * scaleY,
        badge.square ? 0.0f : 3.0f * scaleY), true);
    SkPaint paint;
    paint.setColor(0xff555555);
    canvas->drawRect(SkRect::MakeLTRB(rect.left(), rect.top(), split, rect.bottom()), paint);
    paint.setColor(badge.color);
    canvas->drawRect(SkRect::MakeLTRB(split, rect.top(), rect.right(), rect.bottom()), paint);
    SkFont font(face, 11.0f * scaleY);
    // Keep glyph proportions identical for neighboring badges. Rounding a
    // badge's outer width must not introduce per-badge horizontal font scaling.
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setSubpixel(false);
    float baseline = rect.top() + 14.0f * scaleY;
    if (snapToPixels) baseline = std::round(baseline);
    auto text = [&](const std::string& value, float left, float right, SkColor color) {
        float x = left + (right - left - font.measureText(value.data(), value.size(), SkTextEncoding::kUTF8)) * 0.5f;
        if (snapToPixels) x = std::round(x);
        paint.setColor(color);
        canvas->drawSimpleText(value.data(), value.size(), SkTextEncoding::kUTF8, x, baseline, font, paint);
    };
    text(badge.label, rect.left(), split, SK_ColorWHITE);
    const unsigned brightness = 299 * SkColorGetR(badge.color) + 587 * SkColorGetG(badge.color) + 114 * SkColorGetB(badge.color);
    text(badge.message, split, rect.right(), brightness > 160000 ? 0xff202020 : SK_ColorWHITE);
}
} // namespace mdviewer
