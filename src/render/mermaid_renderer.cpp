#include "render/mermaid_renderer.h"
#include "render/mermaid_svg.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkStream.h"
#include "modules/skshaper/include/SkShaper_factory.h"
#include "modules/svg/include/SkSVGDOM.h"

#if MDVIEWER_ENABLE_MERMAID
extern "C" {
struct MermaidResult {
    uint8_t* light;
    size_t lightLength;
    uint8_t* dark;
    size_t darkLength;
    float width;
    float height;
    uint32_t status;
};
MermaidResult mdv_mermaid_render(const uint8_t*, size_t, const uint8_t*, size_t);
void mdv_mermaid_free(uint8_t*, size_t);
}
#endif

namespace mdviewer {

struct MermaidRenderData {
    sk_sp<SkSVGDOM> light;
    sk_sp<SkSVGDOM> dark;
    float width = 0.0f;
    float height = 0.0f;
    std::mutex mutex;
};

namespace {

#if MDVIEWER_ENABLE_MERMAID
constexpr size_t kMaxSourceBytes = 16 * 1024;
constexpr size_t kMaxCacheEntries = 64;
constexpr size_t kMaxCachedSvgBytes = 16 * 1024 * 1024;

struct CachedDiagram {
    std::shared_ptr<MermaidRenderData> data;
    std::string error;
};

struct RustResultOwner {
    MermaidResult result;
    ~RustResultOwner() {
        mdv_mermaid_free(result.light, result.lightLength);
        mdv_mermaid_free(result.dark, result.darkLength);
    }
};

CachedDiagram GetDiagram(std::string_view source, std::string_view family, SkFontMgr* fonts) {
    // The number of retained SVGs and their source bytes is bounded. Old layouts
    // keep their own shared ownership if cache entries are evicted.
    static std::mutex mutex;
    static std::map<std::pair<std::string, std::string>, CachedDiagram> cache;
    static size_t cachedBytes = 0;
    std::lock_guard lock(mutex);
    const auto key = std::make_pair(std::string(source), std::string(family));
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second;
    }
    RustResultOwner owner{mdv_mermaid_render(
        reinterpret_cast<const uint8_t*>(source.data()), source.size(),
        reinterpret_cast<const uint8_t*>(family.data()), family.size())};
    const MermaidResult& result = owner.result;
    CachedDiagram entry;
    if (result.status != 0) {
        entry.error.assign(reinterpret_cast<const char*>(result.light), result.lightLength);
    } else {
        std::string lightSvg;
        std::string darkSvg;
        const bool supported = NormalizeMermaidSvg(std::string_view(reinterpret_cast<const char*>(result.light), result.lightLength), lightSvg, entry.error) &&
            NormalizeMermaidSvg(std::string_view(reinterpret_cast<const char*>(result.dark), result.darkLength), darkSvg, entry.error);
        if (supported) {
            auto data = std::make_shared<MermaidRenderData>();
            SkSVGDOM::Builder builder;
            builder.setFontManager(sk_ref_sp(fonts));
            builder.setTextShapingFactory(SkShapers::Primitive::Factory());
            SkMemoryStream light(lightSvg.data(), lightSvg.size(), true);
            SkMemoryStream dark(darkSvg.data(), darkSvg.size(), true);
            data->light = builder.make(light);
            data->dark = builder.make(dark);
            if (data->light && data->dark && std::isfinite(result.width) &&
                std::isfinite(result.height) && result.width > 0.0f && result.height > 0.0f) {
                data->width = result.width;
                data->height = result.height;
                entry.data = std::move(data);
            } else {
                entry.error = "Could not load the rendered Mermaid SVG.";
            }
        }
    }
    const size_t bytes = result.lightLength + result.darkLength + source.size() + family.size();
    if (cache.size() >= kMaxCacheEntries || cachedBytes + bytes > kMaxCachedSvgBytes) {
        cache.clear();
        cachedBytes = 0;
    }
    cache.emplace(key, entry);
    cachedBytes += bytes;
    return entry;
}
#endif

} // namespace

MermaidLayout LayoutMermaid(
    std::string_view source, std::string_view fontFamily,
    SkFontMgr* fontManager, float baseFontSize, float maximumWidth) {
    MermaidLayout result;
#if MDVIEWER_ENABLE_MERMAID
    if (source.size() > kMaxSourceBytes) {
        result.error = "Diagram exceeds the source limit (16 KiB).";
        return result;
    }
    if (!fontManager || !std::isfinite(maximumWidth) || maximumWidth <= 0.0f ||
        !std::isfinite(baseFontSize) || baseFontSize <= 0.0f) {
        result.error = "Invalid Mermaid layout dimensions or font context.";
        return result;
    }
    const auto cached = GetDiagram(source, fontFamily, fontManager);
    result.renderData = cached.data;
    result.error = cached.error;
    if (result.renderData) {
        const float scale = std::min(baseFontSize / 17.0f, maximumWidth / result.renderData->width);
        result.width = result.renderData->width * scale;
        result.height = result.renderData->height * scale;
    }
#else
    (void)source; (void)fontFamily; (void)fontManager; (void)baseFontSize; (void)maximumWidth;
    result.error = "Mermaid support is disabled in this build.";
#endif
    return result;
}

void DrawMermaid(SkCanvas* canvas, const MermaidLayout& layout, float x, float y, bool dark) {
    if (!canvas || !layout.renderData) {
        return;
    }
    auto& data = *layout.renderData;
    std::lock_guard lock(data.mutex);
    auto& dom = dark ? data.dark : data.light;
    canvas->save();
    canvas->translate(x, y);
    canvas->clipRect(SkRect::MakeWH(layout.width, layout.height));
    canvas->scale(layout.width / data.width, layout.height / data.height);
    dom->setContainerSize(SkSize::Make(data.width, data.height));
    dom->render(canvas);
    canvas->restore();
}

} // namespace mdviewer
