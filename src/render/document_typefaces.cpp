#include "render/document_typefaces.h"

#include <utility>

#include "util/skia_font_utils.h"

namespace mdviewer {

bool DocumentTypefaceCache::EnsureInitialized(const std::string& preferredFontFamilyUtf8) {
    if (preferredFontFamilyUtf8_ != preferredFontFamilyUtf8) {
        preferredFontFamilyUtf8_ = preferredFontFamilyUtf8;
        ResetResolvedTypefaces();
    }

    if (!fontMgr_) {
        fontMgr_ = CreateFontManager();
    }

    if (!regular_) {
        regular_ = CreateDocumentTypeface(SkFontStyle::Normal());
    }

    if (!bold_) {
        bold_ = CreateDocumentTypeface(SkFontStyle::Bold());
        if (!bold_) {
            bold_ = regular_;
        }
    }

    if (!heading_) {
        heading_ = bold_ ? bold_ : regular_;
    }

    if (!code_) {
        code_ = CreatePreferredTypeface(
            fontMgr_,
            {"Cascadia Mono", "Consolas", "JetBrains Mono", "Courier New", "monospace"});
        if (!code_) {
            code_ = regular_;
        }
    }

    return fontMgr_ != nullptr &&
           regular_ != nullptr &&
           bold_ != nullptr &&
           heading_ != nullptr &&
           code_ != nullptr;
}

void DocumentTypefaceCache::Reset() {
    preferredFontFamilyUtf8_.clear();
    fontMgr_.reset();
    ResetResolvedTypefaces();
}

DocumentTypefaceSet DocumentTypefaceCache::GetTypefaceSet() const {
    return DocumentTypefaceSet{
        .regular = regular_.get(),
        .bold = bold_.get(),
        .heading = heading_.get(),
        .code = code_.get(),
    };
}

SkTypeface* DocumentTypefaceCache::GetRegularTypeface() const {
    return regular_.get();
}

SkTypeface* DocumentTypefaceCache::GetOrCreateTypeface(const std::string& familyNameUtf8, SkFontStyle style) {
    if (!fontMgr_) {
        fontMgr_ = CreateFontManager();
    }

    if (preferredFontFamilyUtf8_ != familyNameUtf8) {
        preferredFontFamilyUtf8_ = familyNameUtf8;
        ResetResolvedTypefaces();
    }

    if (familyNameUtf8.empty()) {
        if (!regular_) {
            regular_ = CreateDefaultTypeface(fontMgr_, style);
        }
        return regular_.get();
    }

    if (!regular_) {
        regular_ = CreateStyledTypeface(fontMgr_, familyNameUtf8.c_str(), style);
        if (!regular_) {
            regular_ = CreateDefaultTypeface(fontMgr_, style);
        }
    }

    return regular_.get();
}

SkTypeface* DocumentTypefaceCache::GetOrCreateTypeface(const std::string& familyNameUtf8, InlineStyle style) {
    SkFontStyle skStyle = SkFontStyle::Normal();
    if (style == InlineStyle::Strong) {
        skStyle = SkFontStyle::Bold();
    }
    return GetOrCreateTypeface(familyNameUtf8, skStyle);
}

sk_sp<SkTypeface> DocumentTypefaceCache::CreateDocumentTypeface(SkFontStyle style) const {
    if (!fontMgr_) {
        return nullptr;
    }

    if (!preferredFontFamilyUtf8_.empty()) {
        if (auto typeface = CreateStyledTypeface(fontMgr_, preferredFontFamilyUtf8_.c_str(), style)) {
            return typeface;
        }
    }

    return CreateDefaultTypeface(fontMgr_, style);
}

void DocumentTypefaceCache::ResetResolvedTypefaces() {
    regular_.reset();
    bold_.reset();
    heading_.reset();
    code_.reset();
}

} // namespace mdviewer
