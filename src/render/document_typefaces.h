#pragma once

#include <string>

#include "text/document_fonts.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

class DocumentTypefaceCache {
public:
    bool EnsureInitialized(const std::string& preferredFontFamilyUtf8);
    void Reset();

    DocumentTypefaceSet GetTypefaceSet() const;
    SkTypeface* GetRegularTypeface() const;
    SkTypeface* GetUiTypeface() const;
    SkTypeface* GetOrCreateTypeface(const std::string& familyNameUtf8, SkFontStyle style);

private:
    sk_sp<SkTypeface> CreateDocumentTypeface(SkFontStyle style) const;
    void ResetResolvedTypefaces();

    std::string preferredFontFamilyUtf8_;
    sk_sp<SkFontMgr> fontMgr_;
    sk_sp<SkTypeface> regular_;
    sk_sp<SkTypeface> bold_;
    sk_sp<SkTypeface> heading_;
    sk_sp<SkTypeface> code_;
    sk_sp<SkTypeface> ui_;
};

} // namespace mdviewer
