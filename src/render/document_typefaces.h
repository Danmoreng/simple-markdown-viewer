#pragma once

#include <string>

#include "layout/document_model.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"

namespace mdviewer {

struct DocumentTypefaceSet {
    SkTypeface* regular = nullptr;
    SkTypeface* bold = nullptr;
    SkTypeface* heading = nullptr;
    SkTypeface* code = nullptr;
};

class DocumentTypefaceCache {
public:
    bool EnsureInitialized(const std::string& preferredFontFamilyUtf8);
    void Reset();

    DocumentTypefaceSet GetTypefaceSet() const;
    SkTypeface* GetRegularTypeface() const;
    SkTypeface* GetUiTypeface() const;
    SkTypeface* GetOrCreateTypeface(const std::string& familyNameUtf8, SkFontStyle style);
    SkTypeface* GetOrCreateTypeface(const std::string& familyNameUtf8, InlineStyle style);

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
