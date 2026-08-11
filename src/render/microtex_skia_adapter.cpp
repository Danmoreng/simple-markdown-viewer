#include "render/microtex_skia_adapter.h"

#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#pragma warning(push)
#pragma warning(disable: 4099 4100 4996)
#include "graphic/graphic.h"
#include "utils/utf.h"
#pragma warning(pop)
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkM44.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "util/skia_font_utils.h"

namespace tex {

class MicroTeXSkiaFont final : public Font {
public:
    static SkFont::Edging edging;
    static SkFontHinting hinting;

    MicroTeXSkiaFont(const std::string& family, int style, float size)
        : MicroTeXSkiaFont(LoadTypefaceFromName(family, style), size) {}

    MicroTeXSkiaFont(const std::string& file, float size)
        : MicroTeXSkiaFont(LoadTypefaceFromFile(file), size) {}

    float getSize() const override { return font_.getSize(); }

    sptr<Font> deriveFont(int style) const override {
        return sptr<Font>(new MicroTeXSkiaFont(GetFamily(), style, getSize()));
    }

    bool operator==(const Font& other) const override {
        const auto* rhs = dynamic_cast<const MicroTeXSkiaFont*>(&other);
        return rhs != nullptr && GetFamily() == rhs->GetFamily() &&
            getSize() == rhs->getSize() && GetStyle() == rhs->GetStyle();
    }

    bool operator!=(const Font& other) const override { return !(*this == other); }

    const SkFont& GetSkFont() const { return font_; }

private:
    explicit MicroTeXSkiaFont(sk_sp<SkTypeface> typeface, float size) {
        if (!typeface) {
            throw std::runtime_error("MicroTeX could not create a Skia typeface");
        }
        font_.setTypeface(std::move(typeface));
        font_.setSubpixel(true);
        font_.setHinting(hinting);
        font_.setEdging(edging);
        font_.setSize(size);
    }

    static sk_sp<SkTypeface> LoadTypefaceFromName(const std::string& family, int style) {
        const auto key = std::make_pair(family, style);
        if (const auto found = namedTypefaces_.find(key); found != namedTypefaces_.end()) {
            return found->second;
        }

        const SkFontStyle fontStyle(
            (style & BOLD) != 0 ? SkFontStyle::kBold_Weight : SkFontStyle::kNormal_Weight,
            SkFontStyle::kNormal_Width,
            (style & ITALIC) != 0 ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant);
        if (!fontManager_) {
            fontManager_ = mdviewer::CreateFontManager();
        }
        sk_sp<SkTypeface> typeface = mdviewer::CreateStyledTypeface(
            fontManager_,
            family.empty() ? "sans-serif" : family.c_str(),
            fontStyle);
        if (!typeface) {
            typeface = mdviewer::CreateDefaultTypeface(fontManager_, fontStyle);
        }
        namedTypefaces_[key] = typeface;
        return typeface;
    }

    static sk_sp<SkTypeface> LoadTypefaceFromFile(const std::string& file) {
        if (const auto found = fileTypefaces_.find(file); found != fileTypefaces_.end()) {
            return found->second;
        }
        if (!fontManager_) {
            fontManager_ = mdviewer::CreateFontManager();
        }
        sk_sp<SkTypeface> typeface = fontManager_ ? fontManager_->makeFromFile(file.c_str()) : nullptr;
        if (!typeface) {
            throw std::runtime_error("MicroTeX could not load font: " + file);
        }
        fileTypefaces_[file] = typeface;
        return typeface;
    }

    std::string GetFamily() const {
        SkString family;
        font_.getTypeface()->getFamilyName(&family);
        return family.c_str();
    }

    int GetStyle() const {
        int style = PLAIN;
        const SkFontStyle fontStyle = font_.getTypeface()->fontStyle();
        if (fontStyle.weight() >= SkFontStyle::kSemiBold_Weight) style |= BOLD;
        if (fontStyle.slant() != SkFontStyle::kUpright_Slant) style |= ITALIC;
        return style;
    }

    SkFont font_;
    static sk_sp<SkFontMgr> fontManager_;
    static std::map<std::pair<std::string, int>, sk_sp<SkTypeface>> namedTypefaces_;
    static std::map<std::string, sk_sp<SkTypeface>> fileTypefaces_;
};

SkFont::Edging MicroTeXSkiaFont::edging = SkFont::Edging::kAntiAlias;
SkFontHinting MicroTeXSkiaFont::hinting = SkFontHinting::kNone;
sk_sp<SkFontMgr> MicroTeXSkiaFont::fontManager_;
std::map<std::pair<std::string, int>, sk_sp<SkTypeface>> MicroTeXSkiaFont::namedTypefaces_;
std::map<std::string, sk_sp<SkTypeface>> MicroTeXSkiaFont::fileTypefaces_;

class MicroTeXSkiaTextLayout final : public TextLayout {
public:
    MicroTeXSkiaTextLayout(const std::wstring& source, const sptr<MicroTeXSkiaFont>& font)
        : font_(font->GetSkFont()), text_(wide2utf8(source)) {}

    void getBounds(Rect& bounds) override {
        SkRect glyphBounds;
        const float advance = font_.measureText(
            text_.c_str(), text_.size(), SkTextEncoding::kUTF8, &glyphBounds);
        SkFontMetrics metrics;
        font_.getMetrics(&metrics);
        bounds.x = glyphBounds.left();
        bounds.y = metrics.fAscent;
        bounds.w = advance;
        bounds.h = metrics.fDescent - metrics.fAscent;
    }

    void draw(Graphics2D& graphics, float x, float y) override;

private:
    SkFont font_;
    std::string text_;
};

class MicroTeXSkiaGraphics final : public Graphics2D {
public:
    explicit MicroTeXSkiaGraphics(SkCanvas* canvas)
        : canvas_(canvas), initialMatrix_(canvas->getLocalToDevice()) {
        paint_.setAntiAlias(true);
        setColor(BLACK);
        setStroke(Stroke());
        setFont(&DefaultFont());
    }

    void setColor(color value) override { color_ = value; paint_.setColor(value); }
    color getColor() const override { return color_; }

    void setStroke(const Stroke& stroke) override {
        stroke_ = stroke;
        paint_.setStrokeWidth(stroke.lineWidth);
        paint_.setStrokeMiter(stroke.miterLimit);
        paint_.setStrokeCap(stroke.cap == CAP_ROUND
            ? SkPaint::kRound_Cap
            : stroke.cap == CAP_SQUARE ? SkPaint::kSquare_Cap : SkPaint::kButt_Cap);
        paint_.setStrokeJoin(stroke.join == JOIN_BEVEL
            ? SkPaint::kBevel_Join
            : stroke.join == JOIN_ROUND ? SkPaint::kRound_Join : SkPaint::kMiter_Join);
    }

    const Stroke& getStroke() const override { return stroke_; }
    void setStrokeWidth(float width) override { stroke_.lineWidth = width; paint_.setStrokeWidth(width); }
    const Font* getFont() const override { return font_; }
    void setFont(const Font* font) override { font_ = static_cast<const MicroTeXSkiaFont*>(font); }
    void translate(float dx, float dy) override { canvas_->translate(dx, dy); }
    void scale(float sx, float sy) override { scaleX_ *= sx; scaleY_ *= sy; canvas_->scale(sx, sy); }

    void rotate(float angle) override { canvas_->rotate(ToDegrees(angle)); }
    void rotate(float angle, float px, float py) override {
        canvas_->translate(px, py);
        canvas_->rotate(ToDegrees(angle));
        canvas_->translate(-px, -py);
    }

    void reset() override { canvas_->setMatrix(initialMatrix_); scaleX_ = scaleY_ = 1.0f; }
    float sx() const override { return scaleX_; }
    float sy() const override { return scaleY_; }

    void drawChar(wchar_t character, float x, float y) override {
        drawText(std::wstring(1, character), x, y);
    }

    void drawText(const std::wstring& text, float x, float y) override {
        paint_.setStyle(SkPaint::kFill_Style);
        const std::string utf8 = wide2utf8(text);
        canvas_->drawString(utf8.c_str(), x, y, font_->GetSkFont(), paint_);
    }

    void drawLine(float x1, float y1, float x2, float y2) override {
        paint_.setStyle(SkPaint::kStroke_Style);
        canvas_->drawLine(x1, y1, x2, y2, paint_);
    }

    void drawRect(float x, float y, float width, float height) override {
        paint_.setStyle(SkPaint::kStroke_Style);
        canvas_->drawRect(SkRect::MakeXYWH(x, y, width, height), paint_);
    }

    void fillRect(float x, float y, float width, float height) override {
        paint_.setStyle(SkPaint::kFill_Style);
        canvas_->drawRect(SkRect::MakeXYWH(x, y, width, height), paint_);
    }

    void drawRoundRect(float x, float y, float width, float height, float rx, float ry) override {
        paint_.setStyle(SkPaint::kStroke_Style);
        canvas_->drawRoundRect(SkRect::MakeXYWH(x, y, width, height), rx, ry, paint_);
    }

    void fillRoundRect(float x, float y, float width, float height, float rx, float ry) override {
        paint_.setStyle(SkPaint::kFill_Style);
        canvas_->drawRoundRect(SkRect::MakeXYWH(x, y, width, height), rx, ry, paint_);
    }

    SkCanvas* Canvas() const { return canvas_; }
    const SkPaint& Paint() const { return paint_; }

private:
    static float ToDegrees(float radians) {
        constexpr float kRadiansToDegrees = 57.29577951308232f;
        return radians * kRadiansToDegrees;
    }

    static MicroTeXSkiaFont& DefaultFont() {
        static MicroTeXSkiaFont font("sans-serif", PLAIN, 20.0f);
        return font;
    }

    SkCanvas* canvas_ = nullptr;
    SkM44 initialMatrix_;
    SkPaint paint_;
    color color_ = BLACK;
    Stroke stroke_;
    const MicroTeXSkiaFont* font_ = nullptr;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
};

void MicroTeXSkiaTextLayout::draw(Graphics2D& graphics, float x, float y) {
    auto& skiaGraphics = static_cast<MicroTeXSkiaGraphics&>(graphics);
    skiaGraphics.Canvas()->drawString(text_.c_str(), x, y, font_, skiaGraphics.Paint());
}

Font* Font::create(const std::string& file, float size) {
    return new MicroTeXSkiaFont(file, size);
}

sptr<Font> Font::_create(const std::string& name, int style, float size) {
    return sptr<Font>(new MicroTeXSkiaFont(name, style, size));
}

sptr<TextLayout> TextLayout::create(const std::wstring& source, const sptr<Font>& font) {
    return sptr<TextLayout>(new MicroTeXSkiaTextLayout(
        source,
        std::static_pointer_cast<MicroTeXSkiaFont>(font)));
}

} // namespace tex

namespace mdviewer {

std::shared_ptr<tex::Graphics2D> CreateMicroTeXSkiaGraphics(SkCanvas* canvas) {
    return std::make_shared<tex::MicroTeXSkiaGraphics>(canvas);
}

} // namespace mdviewer
