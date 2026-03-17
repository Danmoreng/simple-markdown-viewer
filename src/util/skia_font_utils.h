#pragma once

#include <initializer_list>

#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

sk_sp<SkFontMgr> CreateFontManager();
sk_sp<SkTypeface> CreateDefaultTypeface(const sk_sp<SkFontMgr>& fontMgr);
sk_sp<SkTypeface> CreatePreferredTypeface(const sk_sp<SkFontMgr>& fontMgr,
                                         std::initializer_list<const char*> familyNames);

} // namespace mdviewer
