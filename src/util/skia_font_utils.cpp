#include "util/skia_font_utils.h"

#include "include/core/SkFontStyle.h"
#include "include/core/SkFontMgr.h"

#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#else
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif

namespace mdviewer {

sk_sp<SkFontMgr> CreateFontManager() {
#ifdef _WIN32
    if (auto fontMgr = SkFontMgr_New_DirectWrite()) {
        return fontMgr;
    }
    return SkFontMgr_New_GDI();
#else
    return SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#endif
}

SkiaFontSystem* CreateSkiaFontSystem() {
    auto fontMgr = CreateFontManager();
    if (!fontMgr) {
        return nullptr;
    }
    return new SkiaFontSystem{std::move(fontMgr)};
}

sk_sp<SkTypeface> CreateDefaultTypeface(const sk_sp<SkFontMgr>& fontMgr, SkFontStyle style) {
    if (!fontMgr) {
        return nullptr;
    }

    const char* familyNames[] = {
        "Inter", "Segoe UI", "San Francisco", "Helvetica Neue", "Arial", "sans-serif"
    };

    for (const char* familyName : familyNames) {
        if (auto typeface = fontMgr->matchFamilyStyle(familyName, style)) {
            return typeface;
        }
    }

    return fontMgr->matchFamilyStyle(nullptr, style);
}

sk_sp<SkTypeface> CreateStyledTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                       const char* familyName,
                                       SkFontStyle style) {
    if (!fontMgr || !familyName) {
        return nullptr;
    }

    return fontMgr->matchFamilyStyle(familyName, style);
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
