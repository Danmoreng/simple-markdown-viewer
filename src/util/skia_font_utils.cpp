#include "util/skia_font_utils.h"

#include "include/core/SkFontStyle.h"
#include "include/ports/SkTypeface_win.h"

namespace mdviewer {

sk_sp<SkFontMgr> CreateFontManager() {
    if (auto fontMgr = SkFontMgr_New_DirectWrite()) {
        return fontMgr;
    }

    return SkFontMgr_New_GDI();
}

sk_sp<SkTypeface> CreateDefaultTypeface(const sk_sp<SkFontMgr>& fontMgr) {
    if (!fontMgr) {
        return nullptr;
    }

    if (auto typeface = fontMgr->matchFamilyStyle(nullptr, SkFontStyle::Normal())) {
        return typeface;
    }

    return fontMgr->legacyMakeTypeface(nullptr, SkFontStyle::Normal());
}

sk_sp<SkTypeface> CreatePreferredTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                         std::initializer_list<const char*> familyNames) {
    if (!fontMgr) {
        return nullptr;
    }

    for (const char* familyName : familyNames) {
        if (auto typeface = fontMgr->matchFamilyStyle(familyName, SkFontStyle::Normal())) {
            return typeface;
        }
    }

    return nullptr;
}

} // namespace mdviewer
