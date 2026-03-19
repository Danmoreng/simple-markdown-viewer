#pragma once

#include <string>

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#pragma warning(pop)

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

private:
    sk_sp<SkTypeface> CreateDocumentTypeface(SkFontStyle style) const;
    void ResetResolvedTypefaces();

    std::string preferredFontFamilyUtf8_;
    sk_sp<SkFontMgr> fontMgr_;
    sk_sp<SkTypeface> regular_;
    sk_sp<SkTypeface> bold_;
    sk_sp<SkTypeface> heading_;
    sk_sp<SkTypeface> code_;
};

} // namespace mdviewer
