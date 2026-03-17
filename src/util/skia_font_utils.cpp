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

sk_sp<SkTypeface> CreateDefaultTypeface(const sk_sp<SkFontMgr>& fontMgr, SkFontStyle style) {
    if (!fontMgr) {
        return nullptr;
    }

    if (auto typeface = fontMgr->matchFamilyStyle(nullptr, style)) {
        return typeface;
    }

    return fontMgr->legacyMakeTypeface(nullptr, style);
}

sk_sp<SkTypeface> CreateStyledTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                       const char* familyName,
                                       SkFontStyle style) {
    if (!fontMgr || !familyName || familyName[0] == '\0') {
        return nullptr;
    }

    if (auto typeface = fontMgr->matchFamilyStyle(familyName, style)) {
        return typeface;
    }

    return fontMgr->legacyMakeTypeface(familyName, style);
}

sk_sp<SkTypeface> CreatePreferredTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                         std::initializer_list<const char*> familyNames,
                                         SkFontStyle style) {
    if (!fontMgr) {
        return nullptr;
    }

    for (const char* familyName : familyNames) {
        if (auto typeface = CreateStyledTypeface(fontMgr, familyName, style)) {
            return typeface;
        }
    }

    return nullptr;
}

} // namespace mdviewer
