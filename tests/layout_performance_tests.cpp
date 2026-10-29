#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>

#include "layout/layout_engine.h"
#include "text/shaped_text_layout.h"
#include "text/source_label.h"
#include "util/skia_font_utils.h"
#include "include/core/SkSurface.h"

namespace mdviewer::tests {
namespace {

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

DocumentModel Paragraph(std::string text) {
    DocumentModel document;
    Block block;
    block.type = BlockType::Paragraph;
    block.inlineRuns.push_back(InlineRun{.text = std::move(text)});
    document.blocks.push_back(std::move(block));
    return document;
}

const RunLayout& ImageRun(const DocumentLayout& layout) {
    for (const auto& block : layout.blocks) {
        for (const auto& line : block.lines) {
            for (const auto& run : line.runs) {
                if (run.kind == InlineKind::Image) return run;
            }
        }
    }
    throw std::runtime_error("Expected image placeholder run");
}

} // namespace

void RunLayoutPerformanceTests() {
    const auto fontManager = CreateFontManager();
    const auto regular = CreateDefaultTypeface(fontManager);
    Require(fontManager != nullptr && regular != nullptr, "Layout regressions require document fonts");
    const DocumentTypefaceSet typefaces{
        fontManager.get(), regular.get(), regular.get(), regular.get(), regular.get()};

    // Crossing the shaping safety limit must not select a quadratic fallback.
    // Fixed-width long tokens exercise both repeated newline scans and prefix
    // measurements. Check output as well as a generous scaling envelope.
    const auto timedLayout = [&](size_t bytes) {
        const auto document = Paragraph(std::string(bytes, 'x'));
        const auto start = std::chrono::steady_clock::now();
        const auto layout = LayoutEngine::ComputeLayout(document, 420.0f, typefaces, 17.0f);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Require(layout.plainText == document.blocks.front().inlineRuns.front().text,
            "Large paragraph fallback must preserve every source byte");
        Require(layout.blocks.front().lines.size() > 1000,
            "Large paragraph fixture must wrap into many visual lines");
        return elapsed;
    };
    const double oneMiB = timedLayout(kMaximumShapingParagraphBytes + 1);
    const double twoMiB = timedLayout((kMaximumShapingParagraphBytes + 1) * 2);
    std::cout << "[layout performance] 1 MiB=" << oneMiB << "s, 2 MiB=" << twoMiB << "s\n";
    Require(twoMiB < 15.0, "A 2 MiB paragraph exceeded the large-document regression budget");
    Require(twoMiB < oneMiB * 3.5 + 0.5,
        "Doubling a long paragraph must not restore quadratic wrapping behavior");

    // With shaping unavailable, emergency wraps still respect complete graphemes.
    std::string combiningText;
    for (int index = 0; index < 3000; ++index) combiningText += "e\xCC\x81";
    auto fallbackTypefaces = typefaces;
    fallbackTypefaces.fontMgr = nullptr;
    const auto combining = LayoutEngine::ComputeLayout(
        Paragraph(combiningText), 80.0f, fallbackTypefaces, 17.0f);
    Require(combining.plainText == combiningText, "Legacy grapheme wrapping must preserve UTF-8 text");
    for (const auto& line : combining.blocks.front().lines) {
        Require(line.textStart % 3 == 0 && line.textLength % 3 == 0,
            "Fallback wrapping must not detach a combining mark from its base");
    }

    std::stop_source cancelled;
    cancelled.request_stop();
    LayoutOptions cancelledOptions;
    cancelledOptions.stopToken = cancelled.get_token();
    bool didCancel = false;
    try {
        (void)LayoutEngine::ComputeLayout(Paragraph("cancelled"), 420, typefaces, 17, {}, cancelledOptions);
    } catch (const LayoutCancelled&) {
        didCancel = true;
    }
    Require(didCancel, "Cancelled layout must report cancellation rather than commit partial output");

    // Cancellation after work has begun must reach the inner wrapping/glyph loops.
    const auto longDocument = Paragraph(std::string(kMaximumShapingParagraphBytes * 4, 'x'));
    std::stop_source stop;
    LayoutOptions activeOptions;
    activeOptions.stopToken = stop.get_token();
    std::jthread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        stop.request_stop();
    });
    didCancel = false;
    const auto cancelStart = std::chrono::steady_clock::now();
    try {
        (void)LayoutEngine::ComputeLayout(longDocument, 420, typefaces, 17, {}, activeOptions);
    } catch (const LayoutCancelled&) {
        didCancel = true;
    }
    Require(didCancel, "In-flight large document layout should observe its stop token");
    Require(std::chrono::steady_clock::now() - cancelStart < std::chrono::seconds(5),
        "In-flight layout cancellation must complete promptly");

    const std::string remote = "https://example.test/" + std::string(600, 'a') + "?token=full-exact-source";
    DocumentModel images;
    Block block;
    block.type = BlockType::Paragraph;
    block.inlineRuns.push_back(InlineRun{.kind = InlineKind::Image, .text = "Remote image", .imageSource = remote});
    images.blocks.push_back(std::move(block));
    LayoutOptions imageOptions;
    imageOptions.imageLoadInfoProvider = [](const std::string& source) {
        return ImageLoadInfo{
            .kind = ImageSourceKind::RemoteUrl,
            .state = ImageLoadState::Blocked,
            .originalSource = source,
            .displayTarget = source,
        };
    };
    for (const auto& fonts : {typefaces, fallbackTypefaces}) {
        const auto imageLayout = LayoutEngine::ComputeLayout(images, 280, fonts, 17, {}, imageOptions);
        const auto& run = ImageRun(imageLayout);
        Require(run.imageLoadRequired, "Remote source placeholder must expose consent state");
        Require(run.imageSourceLines.size() > 10, "Long remote source must wrap rather than truncate");
        const std::string displayed = std::accumulate(
            run.imageSourceLines.begin(), run.imageSourceLines.end(), std::string{});
        Require(displayed == remote, "Placeholder must display the exact complete source URL");
        const float requiredHeight = kImagePlaceholderPadding * 2 + run.imagePlaceholderLabelHeight +
            run.imageSourceLineHeight * static_cast<float>(run.imageSourceLines.size()) + run.imageLoadButtonHeight;
        Require(run.imageHeight >= requiredHeight, "Placeholder must reserve all source rows and consent controls");
    }

    // Source drawing uses per-codepoint fallback fonts. The wrapping measurement
    // must use those same fonts, including glyphs absent from the regular face.
    std::string unicodeSource = "https://example.test/";
    for (int index = 0; index < 35; ++index) {
        unicodeSource += "\xE6\xBC\xA2\xE5\xAD\x97\xF0\x9F\x9A\x80\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A";
    }
    unicodeSource += "?full=source";
    const auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 320));
    Require(surface != nullptr, "Unicode source fixture requires a drawing surface");
    const SkFont sourceFont = MakeSourceLabelFont(typefaces, kImagePlaceholderSourceFontSize);
    const SkPaint paint;
    for (const std::string& source : {unicodeSource, unicodeSource + "\xE2\x80\xAE.png"}) {
        images.blocks.front().inlineRuns.front().imageSource = source;
        const auto unicodeLayout = LayoutEngine::ComputeLayout(images, 280, typefaces, 17, {}, imageOptions);
        const auto& run = ImageRun(unicodeLayout);
        const std::string displayed = std::accumulate(
            run.imageSourceLines.begin(), run.imageSourceLines.end(), std::string{});
        Require(displayed == EscapeSourceLabelControls(source),
            "Unicode source must remain complete with visible escapes for hidden direction controls");
        Require(run.imageSource == source && run.imageLoadInfo.originalSource == source,
            "Display escaping must never alter the consent/download target");
        Require(run.imageSourceControlsEscaped == (displayed != source),
            "Escaped controls must be identified for the source label");
        for (const auto& line : run.imageSourceLines) {
            const float measured = MeasureSourceLabelText(typefaces, sourceFont, line);
            const float drawn = DrawSourceLabelText(surface->getCanvas(), typefaces, sourceFont,
                line, 0, 20, paint);
            Require(std::abs(measured - drawn) < 0.001f,
                "Source wrapping and source drawing must use identical fallback advances");
            Require(drawn <= run.imageWidth - kImagePlaceholderPadding * 2 + 0.01f,
                "Every complete Unicode source fragment must fit its reserved display width");
        }
    }
    Require(EscapeSourceLabelControls("a\xE2\x80\xAE" "b") == "a\\u202Eb",
        "Bidirectional override must be visible in the external target label");
}

} // namespace mdviewer::tests
