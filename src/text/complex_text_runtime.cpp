#include "text/complex_text_runtime.h"

#include "include/core/SkFontMgr.h"
#include "modules/skshaper/include/SkShaper.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <utility>

namespace mdviewer {

ComplexTextRuntime::ComplexTextRuntime(sk_sp<SkFontMgr> fontManager) {
    if (!fontManager) {
        diagnostic_ = "complex text is unavailable: no font manager";
        return;
    }

    unicode_ = SkUnicodes::ICU::Make();
    if (!unicode_) {
        diagnostic_ = "complex text is unavailable: ICU Unicode services failed to initialize";
        return;
    }

    // ShapeThenWrap in the pinned Skia build evaluates line-break offsets per
    // segmented style run. After a Markdown soft break or formatting boundary,
    // those offsets no longer match the paragraph-global glyph clusters and it
    // can fall back to splitting ordinary words. The shaper-driven wrapper
    // buffers language breaks in paragraph coordinates while retaining the same
    // HarfBuzz, ICU, BiDi, script, and font-run pipeline.
    shaper_ = SkShapers::HB::ShaperDrivenWrapper(unicode_, std::move(fontManager));
    if (!shaper_) {
        diagnostic_ = "complex text is unavailable: HarfBuzz shaper failed to initialize";
        unicode_.reset();
        return;
    }

    diagnostic_ = "complex text runtime is available";
}

ComplexTextRuntime::~ComplexTextRuntime() = default;

bool ComplexTextRuntime::IsAvailable() const noexcept {
    return unicode_ != nullptr && shaper_ != nullptr;
}

const std::string& ComplexTextRuntime::Diagnostic() const noexcept {
    return diagnostic_;
}

SkUnicode* ComplexTextRuntime::Unicode() const noexcept {
    return unicode_.get();
}

SkShaper* ComplexTextRuntime::Shaper() const noexcept {
    return shaper_.get();
}

}  // namespace mdviewer
