#pragma once

#include <initializer_list>

#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

struct SkiaFontSystem {
    sk_sp<SkFontMgr> fontMgr;
};

SkiaFontSystem* CreateSkiaFontSystem();

sk_sp<SkFontMgr> CreateFontManager();
sk_sp<SkTypeface> CreateDefaultTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                        SkFontStyle style = SkFontStyle::Normal());
sk_sp<SkTypeface> CreateStyledTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                       const char* familyName,
                                       SkFontStyle style);
sk_sp<SkTypeface> CreatePreferredTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                         std::initializer_list<const char*> familyNames,
                                         SkFontStyle style = SkFontStyle::Normal());

} // namespace mdviewer
