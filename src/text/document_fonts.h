#pragma once

class SkFontMgr;
class SkTypeface;

namespace mdviewer {

struct DocumentTypefaceSet {
    SkFontMgr* fontMgr = nullptr;
    SkTypeface* regular = nullptr;
    SkTypeface* bold = nullptr;
    SkTypeface* heading = nullptr;
    SkTypeface* code = nullptr;
};

}  // namespace mdviewer
