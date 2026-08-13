#include "text/shaped_text_layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <utf8proc.h>

#include "text/complex_text_runtime.h"
#include "text/document_font_style.h"

#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontTypes.h"
#include "modules/skshaper/include/SkShaper.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#include "modules/skshaper/include/SkShaper_skunicode.h"
#include "modules/skunicode/include/SkUnicode.h"

namespace mdviewer {

namespace {

constexpr char kObjectPlaceholder[] = "\xE2\x80\x83";
constexpr size_t kObjectPlaceholderBytes = sizeof(kObjectPlaceholder) - 1;

std::vector<size_t> GraphemeBoundaries(std::string_view text) {
    std::vector<size_t> boundaries{0};
    size_t offset = 0;
    utf8proc_int32_t previous = 0;
    utf8proc_int32_t state = 0;
    bool hasPrevious = false;

    while (offset < text.size()) {
        utf8proc_int32_t current = 0;
        const utf8proc_ssize_t bytes = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t*>(text.data() + offset),
            static_cast<utf8proc_ssize_t>(text.size() - offset),
            &current);
        if (bytes <= 0) {
            ++offset;
            boundaries.push_back(offset);
            hasPrevious = false;
            state = 0;
            continue;
        }
        if (hasPrevious && utf8proc_grapheme_break_stateful(previous, current, &state)) {
            boundaries.push_back(offset);
        }
        previous = current;
        hasPrevious = true;
        offset += static_cast<size_t>(bytes);
    }
    if (boundaries.back() != text.size()) {
        boundaries.push_back(text.size());
    }
    return boundaries;
}

SkFont ConfigureSpanFont(
    const ShapingSpan& span,
    const DocumentTypefaceSet& typefaces,
    const ShapedTextLayoutOptions& options) {
    SkFont font;
    ConfigureDocumentFont(
        font,
        typefaces,
        options.blockType,
        span.formatting,
        options.baseFontSize);
    font.setSize(font.getSize() * options.fontScale);
    return font;
}

SkFont ConfigureObjectFont(
    const ShapingSpan& span,
    const DocumentTypefaceSet& typefaces,
    const ShapedTextLayoutOptions& options) {
    SkFont font = ConfigureSpanFont(span, typefaces, options);
    font.setSubpixel(true);
    const float placeholderAdvance = font.measureText(
        kObjectPlaceholder,
        kObjectPlaceholderBytes,
        SkTextEncoding::kUTF8);
    if (placeholderAdvance > 0.0f && span.objectWidth > 0.0f) {
        // The pinned HarfBuzz bridge applies SkFont::scaleX while obtaining the
        // glyph advance and again while converting the shaped advance to Skia
        // units. Calibrate for that behavior so the neutral placeholder has the
        // object's exact width in the line breaker.
        font.setScaleX(font.getScaleX() * std::sqrt(span.objectWidth / placeholderAdvance));
    }
    return font;
}

const ShapingSpan* FindSpan(const ShapingParagraph& paragraph, size_t shapingOffset) {
    const auto found = std::upper_bound(
        paragraph.spans.begin(),
        paragraph.spans.end(),
        shapingOffset,
        [](size_t offset, const ShapingSpan& span) {
            return offset < span.shapingStart;
        });
    if (found == paragraph.spans.begin()) {
        return nullptr;
    }
    const ShapingSpan& span = *std::prev(found);
    return shapingOffset < span.shapingStart + span.shapingLength ? &span : nullptr;
}

class StyleAwareFontRunIterator final : public SkShaper::FontRunIterator {
public:
    StyleAwareFontRunIterator(
        const ShapingParagraph& paragraph,
        const DocumentTypefaceSet& typefaces,
        const ShapedTextLayoutOptions& options)
        : paragraph_(paragraph), typefaces_(typefaces), options_(options) {}

    void consume() override {
        if (inner_ && inner_->atEnd()) {
            inner_.reset();
            ++spanIndex_;
        }
        while (!inner_ && spanIndex_ < paragraph_.spans.size()) {
            const ShapingSpan& span = paragraph_.spans[spanIndex_];
            if (span.shapingLength == 0) {
                ++spanIndex_;
                continue;
            }

            SkFont spanFont = span.IsAtomicObject()
                ? ConfigureObjectFont(span, typefaces_, options_)
                : ConfigureSpanFont(span, typefaces_, options_);
            if (span.IsAtomicObject() || typefaces_.fontMgr == nullptr || spanFont.getTypeface() == nullptr) {
                inner_ = std::make_unique<SkShaper::TrivialFontRunIterator>(
                    spanFont,
                    span.shapingLength);
            } else {
                inner_ = SkShaper::MakeFontMgrRunIterator(
                    paragraph_.utf8.data() + span.shapingStart,
                    span.shapingLength,
                    spanFont,
                    sk_ref_sp(typefaces_.fontMgr));
            }
            if (!inner_) {
                return;
            }
        }

        if (!inner_ || inner_->atEnd()) {
            return;
        }
        inner_->consume();
        currentFont_ = inner_->currentFont();
        currentEnd_ = paragraph_.spans[spanIndex_].shapingStart + inner_->endOfCurrentRun();
    }

    size_t endOfCurrentRun() const override {
        return currentEnd_;
    }

    bool atEnd() const override {
        if (!inner_) {
            return paragraph_.spans.empty() || spanIndex_ >= paragraph_.spans.size();
        }
        return spanIndex_ + 1 >= paragraph_.spans.size() && inner_->atEnd();
    }

    const SkFont& currentFont() const override {
        return currentFont_;
    }

private:
    const ShapingParagraph& paragraph_;
    const DocumentTypefaceSet& typefaces_;
    const ShapedTextLayoutOptions& options_;
    size_t spanIndex_ = 0;
    size_t currentEnd_ = 0;
    SkFont currentFont_;
    std::unique_ptr<SkShaper::FontRunIterator> inner_;
};

float BaselineShift(InlineFormatting formatting, float fontSize) {
    if (HasFormatting(formatting, InlineFormatting::Superscript)) {
        return -fontSize * 0.38f;
    }
    if (HasFormatting(formatting, InlineFormatting::Subscript)) {
        return fontSize * 0.2f;
    }
    return 0.0f;
}

struct PendingRunBuffer {
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> positions;
    std::vector<SkPoint> offsets;
    std::vector<uint32_t> clusters;
    float visualX = 0.0f;
};

class ShapedLayoutRunHandler final : public SkShaper::RunHandler {
public:
    ShapedLayoutRunHandler(
        const ShapingParagraph& paragraph,
        const ShapedTextLayoutOptions& options,
        ShapedTextLayoutResult& result)
        : paragraph_(paragraph), options_(options), result_(result), currentY_(options.startY) {}

    void beginLine() override {
        currentLine_ = {};
        currentLine_.y = currentY_;
        currentLine_.direction = paragraph_.direction;
        lineAscent_ = 0.0f;
        lineDescent_ = 0.0f;
        lineLeading_ = 0.0f;
        minimumObjectHeight_ = 0.0f;
        visualX_ = 0.0f;
    }

    void runInfo(const RunInfo& info) override {
        SkFontMetrics metrics;
        info.fFont.getMetrics(&metrics);
        const ShapingSpan* span = FindSpan(paragraph_, info.utf8Range.begin());
        const float shift = span != nullptr ? BaselineShift(span->formatting, info.fFont.getSize()) : 0.0f;
        lineAscent_ = std::max(lineAscent_, -(metrics.fAscent + shift));
        lineDescent_ = std::max(lineDescent_, metrics.fDescent + shift);
        lineLeading_ = std::max(lineLeading_, metrics.fLeading);
        if (span != nullptr && span->IsAtomicObject()) {
            minimumObjectHeight_ = std::max(minimumObjectHeight_, span->objectHeight + 4.0f);
        }
    }

    void commitRunInfo() override {
        const float fontHeight = lineAscent_ + lineDescent_ + lineLeading_;
        lineHeight_ = std::max({fontHeight, minimumObjectHeight_, options_.fallbackLineHeight, 1.0f});
        baseline_ = ((lineHeight_ - fontHeight) * 0.5f) + lineAscent_;
        currentLine_.height = lineHeight_;
    }

    Buffer runBuffer(const RunInfo& info) override {
        pending_ = {};
        pending_.glyphs.resize(info.glyphCount);
        pending_.positions.resize(info.glyphCount);
        pending_.offsets.resize(info.glyphCount);
        pending_.clusters.resize(info.glyphCount);
        pending_.visualX = visualX_;

        const ShapingSpan* span = FindSpan(paragraph_, info.utf8Range.begin());
        const float shift = span != nullptr ? BaselineShift(span->formatting, info.fFont.getSize()) : 0.0f;
        return {
            pending_.glyphs.data(),
            pending_.positions.data(),
            pending_.offsets.data(),
            pending_.clusters.data(),
            {visualX_, baseline_ + shift},
        };
    }

    void commitRunBuffer(const RunInfo& info) override {
        const ShapingSpan* span = FindSpan(paragraph_, info.utf8Range.begin());
        if (span == nullptr || info.utf8Range.end() > span->shapingStart + span->shapingLength) {
            result_.diagnostic = "complex text shaping emitted a run outside its semantic span";
            return;
        }

        RunLayout run;
        run.formatting = span->formatting;
        run.kind = span->kind;
        run.metadataRole = span->metadataRole;
        run.syntaxRole = span->syntaxRole;
        run.imageSource = span->imageSource;
        run.mathSource = span->mathSource;
        run.visualX = pending_.visualX;
        run.visualWidth = span->IsAtomicObject() ? span->objectWidth : info.fAdvance.x();
        run.imageWidth = span->kind == InlineKind::Image ? span->objectWidth : 0.0f;
        run.imageHeight = span->kind == InlineKind::Image ? span->objectHeight : 0.0f;
        run.bidiLevel = info.fBidiLevel;
        run.semanticSpanId = span->semanticSpanId;
        run.mathDisplay = span->mathDisplay;
        run.mathLayout = span->mathLayout;
        run.linkTarget = span->linkTarget;
        run.baselineShift = BaselineShift(span->formatting, info.fFont.getSize());

        if (span->IsAtomicObject()) {
            if (std::abs(info.fAdvance.x() - span->objectWidth) > 0.5f) {
                result_.diagnostic = "complex text object placeholder width calibration failed";
                return;
            }
            run.text = span->text;
            run.textStart = span->logicalStart;
            run.shaped = false;
            if (span->kind == InlineKind::Math) {
                const bool isRtl = (info.fBidiLevel & 1U) != 0;
                run.caretStops = isRtl
                    ? std::vector<CaretStop>{{span->logicalStart + span->logicalLength, run.visualX},
                                             {span->logicalStart, run.visualX + run.visualWidth}}
                    : std::vector<CaretStop>{{span->logicalStart, run.visualX},
                                             {span->logicalStart + span->logicalLength,
                                              run.visualX + run.visualWidth}};
            }
        } else {
            const size_t spanRelativeStart = info.utf8Range.begin() - span->shapingStart;
            run.text = paragraph_.utf8.substr(info.utf8Range.begin(), info.utf8Range.size());
            run.textStart = span->logicalStart + spanRelativeStart;
            run.shaped = true;
            run.shapedFont = info.fFont;
            run.glyphs = pending_.glyphs;
            run.glyphPositions.reserve(pending_.positions.size());
            for (size_t index = 0; index < pending_.positions.size(); ++index) {
                run.glyphPositions.push_back(pending_.positions[index] + pending_.offsets[index]);
            }
            run.glyphClusters.reserve(pending_.clusters.size());
            for (uint32_t cluster : pending_.clusters) {
                const size_t normalized = cluster >= info.utf8Range.begin()
                    ? static_cast<size_t>(cluster) - info.utf8Range.begin()
                    : 0;
                run.glyphClusters.push_back(static_cast<uint32_t>(
                    std::min(normalized, run.text.size())));
            }
            BuildCaretStops(run, info);
        }

        const float visualAdvance = run.visualWidth;
        currentLine_.runs.push_back(std::move(run));
        visualX_ += visualAdvance;
        result_.naturalWidth = std::max(result_.naturalWidth, visualX_);
    }

    void commitLine() override {
        currentLine_.x = 0.0f;
        currentLine_.width = visualX_;

        bool hasLogicalRange = false;
        size_t logicalStart = std::numeric_limits<size_t>::max();
        size_t logicalEnd = 0;
        for (const RunLayout& run : currentLine_.runs) {
            if (run.kind == InlineKind::Image) {
                continue;
            }
            hasLogicalRange = true;
            logicalStart = std::min(logicalStart, run.textStart);
            logicalEnd = std::max(logicalEnd, run.textStart + run.text.size());
        }
        currentLine_.textStart = hasLogicalRange ? logicalStart : paragraph_.logicalStart;
        currentLine_.textLength = hasLogicalRange ? logicalEnd - logicalStart : 0;
        result_.lines.push_back(std::move(currentLine_));
        currentY_ += lineHeight_;
    }

    float TotalHeight() const noexcept {
        return currentY_ - options_.startY;
    }

private:
    void BuildCaretStops(RunLayout& run, const RunInfo& info) {
        const std::vector<size_t> graphemeBoundaries = GraphemeBoundaries(run.text);
        const std::unordered_set<size_t> safeBoundaries(
            graphemeBoundaries.begin(), graphemeBoundaries.end());
        const bool isRtl = (info.fBidiLevel & 1U) != 0;

        size_t glyphIndex = 0;
        while (glyphIndex < pending_.clusters.size()) {
            const uint32_t cluster = pending_.clusters[glyphIndex];
            size_t clusterEndIndex = glyphIndex + 1;
            while (clusterEndIndex < pending_.clusters.size() &&
                   pending_.clusters[clusterEndIndex] == cluster) {
                ++clusterEndIndex;
            }

            const size_t normalized = cluster >= info.utf8Range.begin()
                ? static_cast<size_t>(cluster) - info.utf8Range.begin()
                : 0;
            if (safeBoundaries.contains(normalized)) {
                const float groupLeft = pending_.positions[glyphIndex].x();
                const float groupRight = clusterEndIndex < pending_.positions.size()
                    ? pending_.positions[clusterEndIndex].x()
                    : pending_.visualX + info.fAdvance.x();
                run.caretStops.push_back({
                    run.textStart + normalized,
                    isRtl ? groupRight : groupLeft,
                });
            }
            glyphIndex = clusterEndIndex;
        }

        const float logicalStartX = isRtl
            ? pending_.visualX + info.fAdvance.x()
            : pending_.visualX;
        const float logicalEndX = isRtl
            ? pending_.visualX
            : pending_.visualX + info.fAdvance.x();
        run.caretStops.push_back({run.textStart, logicalStartX});
        run.caretStops.push_back({run.textStart + run.text.size(), logicalEndX});
        std::sort(run.caretStops.begin(), run.caretStops.end(), [](const CaretStop& left, const CaretStop& right) {
            if (left.x != right.x) {
                return left.x < right.x;
            }
            return left.textPosition < right.textPosition;
        });
        run.caretStops.erase(
            std::unique(
                run.caretStops.begin(),
                run.caretStops.end(),
                [](const CaretStop& left, const CaretStop& right) {
                    return left.textPosition == right.textPosition && left.x == right.x;
                }),
            run.caretStops.end());
    }

    const ShapingParagraph& paragraph_;
    const ShapedTextLayoutOptions& options_;
    ShapedTextLayoutResult& result_;
    float currentY_ = 0.0f;
    float visualX_ = 0.0f;
    float lineAscent_ = 0.0f;
    float lineDescent_ = 0.0f;
    float lineLeading_ = 0.0f;
    float minimumObjectHeight_ = 0.0f;
    float lineHeight_ = 0.0f;
    float baseline_ = 0.0f;
    LineLayout currentLine_;
    PendingRunBuffer pending_;
};

void AppendSpan(
    ShapingParagraph& paragraph,
    const ShapedTextInputRun& input,
    std::string_view shapingText,
    std::string text,
    size_t logicalStart,
    size_t logicalLength,
    size_t semanticSpanId) {
    ShapingSpan span;
    span.shapingStart = paragraph.utf8.size();
    span.shapingLength = shapingText.size();
    span.logicalStart = logicalStart;
    span.logicalLength = logicalLength;
    span.semanticSpanId = semanticSpanId;
    span.formatting = input.run.formatting;
    span.kind = input.run.kind == InlineKind::SoftBreak ? InlineKind::Text : input.run.kind;
    span.metadataRole = input.run.metadataRole;
    span.syntaxRole = input.run.syntaxRole;
    span.text = std::move(text);
    span.imageSource = input.run.imageSource;
    span.mathSource = input.run.mathSource;
    span.linkTarget = input.run.linkTarget;
    span.objectWidth = input.objectWidth;
    span.objectHeight = input.objectHeight;
    span.mathDisplay = input.run.mathDisplay;
    span.mathLayout = input.mathLayout;
    paragraph.utf8.append(shapingText);
    paragraph.spans.push_back(std::move(span));
}

} // namespace

bool ShapingSpan::IsAtomicObject() const noexcept {
    return kind == InlineKind::Image || kind == InlineKind::Math;
}

std::optional<ResolvedTextDirection> TryResolveFirstStrongDirection(std::string_view utf8) {
    size_t offset = 0;
    while (offset < utf8.size()) {
        utf8proc_int32_t codepoint = 0;
        const utf8proc_ssize_t bytes = utf8proc_iterate(
            reinterpret_cast<const utf8proc_uint8_t*>(utf8.data() + offset),
            static_cast<utf8proc_ssize_t>(utf8.size() - offset),
            &codepoint);
        if (bytes <= 0) {
            ++offset;
            continue;
        }
        const utf8proc_property_t* property = utf8proc_get_property(codepoint);
        if (property->bidi_class == UTF8PROC_BIDI_CLASS_L) {
            return ResolvedTextDirection::LeftToRight;
        }
        if (property->bidi_class == UTF8PROC_BIDI_CLASS_R ||
            property->bidi_class == UTF8PROC_BIDI_CLASS_AL) {
            return ResolvedTextDirection::RightToLeft;
        }
        offset += static_cast<size_t>(bytes);
    }
    return std::nullopt;
}

ResolvedTextDirection ResolveFirstStrongDirection(std::string_view utf8) {
    return TryResolveFirstStrongDirection(utf8).value_or(ResolvedTextDirection::LeftToRight);
}

ShapingParagraphSet BuildShapingParagraphs(
    const std::vector<ShapedTextInputRun>& runs,
    size_t logicalStart) {
    ShapingParagraphSet result;
    size_t logicalPosition = logicalStart;
    size_t nextSemanticSpanId = 1;
    for (const auto& input : runs) {
        nextSemanticSpanId = std::max(nextSemanticSpanId, input.semanticSpanId + 1);
    }

    ShapingParagraph current;
    current.logicalStart = logicalPosition;
    const auto flush = [&](bool endedByHardBreak) {
        current.logicalLength = logicalPosition - current.logicalStart;
        current.endedByHardBreak = endedByHardBreak;
        current.direction = ResolveFirstStrongDirection(current.utf8);
        result.paragraphs.push_back(std::move(current));
        current = {};
        current.logicalStart = logicalPosition;
    };

    for (const ShapedTextInputRun& input : runs) {
        const size_t semanticSpanId = input.semanticSpanId != 0
            ? input.semanticSpanId
            : nextSemanticSpanId++;
        if (input.run.kind == InlineKind::HardBreak) {
            result.logicalText.push_back('\n');
            ++logicalPosition;
            flush(true);
            current.logicalStart = logicalPosition;
            continue;
        }

        if (input.run.kind == InlineKind::Image) {
            AppendSpan(
                current,
                input,
                std::string_view(kObjectPlaceholder, kObjectPlaceholderBytes),
                input.run.text,
                logicalPosition,
                0,
                semanticSpanId);
            continue;
        }

        if (input.run.kind == InlineKind::Math) {
            if (input.run.mathDisplay && !current.spans.empty()) {
                flush(false);
            }
            const size_t runLogicalStart = logicalPosition;
            result.logicalText += input.run.text;
            logicalPosition += input.run.text.size();
            AppendSpan(
                current,
                input,
                std::string_view(kObjectPlaceholder, kObjectPlaceholderBytes),
                input.run.text,
                runLogicalStart,
                input.run.text.size(),
                semanticSpanId);
            if (input.run.mathDisplay) {
                current.displayMath = true;
                flush(false);
            }
            continue;
        }

        const std::string normalized = input.run.kind == InlineKind::SoftBreak
            ? std::string(" ")
            : input.run.text;
        size_t fragmentStart = 0;
        while (fragmentStart <= normalized.size()) {
            const size_t newline = normalized.find('\n', fragmentStart);
            const size_t fragmentEnd = newline == std::string::npos ? normalized.size() : newline;
            if (fragmentEnd > fragmentStart) {
                const std::string fragment = normalized.substr(fragmentStart, fragmentEnd - fragmentStart);
                const size_t runLogicalStart = logicalPosition;
                result.logicalText += fragment;
                logicalPosition += fragment.size();
                AppendSpan(
                    current,
                    input,
                    fragment,
                    fragment,
                    runLogicalStart,
                    fragment.size(),
                    semanticSpanId);
            }
            if (newline == std::string::npos) {
                break;
            }
            result.logicalText.push_back('\n');
            ++logicalPosition;
            flush(true);
            current.logicalStart = logicalPosition;
            fragmentStart = newline + 1;
        }
    }

    if (!current.spans.empty() || result.paragraphs.empty()) {
        flush(false);
    }
    result.logicalEnd = logicalPosition;
    return result;
}

ShapedTextLayoutResult ShapeTextParagraph(
    const ShapingParagraph& paragraph,
    const ComplexTextRuntime& runtime,
    const DocumentTypefaceSet& typefaces,
    const ShapedTextLayoutOptions& options) {
    ShapedTextLayoutResult result;
    result.direction = paragraph.direction;
    if (!runtime.IsAvailable() || runtime.Unicode() == nullptr || runtime.Shaper() == nullptr) {
        result.diagnostic = runtime.Diagnostic();
        return result;
    }
    if (options.wrapWidth <= 0.0f || !std::isfinite(options.wrapWidth)) {
        result.diagnostic = "complex text shaping requires a finite positive wrap width";
        return result;
    }
    if (options.baseFontSize <= 0.0f || !std::isfinite(options.baseFontSize) ||
        options.fontScale <= 0.0f || !std::isfinite(options.fontScale) ||
        !std::isfinite(options.startY) || !std::isfinite(options.fallbackLineHeight)) {
        result.diagnostic = "complex text shaping received invalid layout metrics";
        return result;
    }
    if (typefaces.fontMgr == nullptr || typefaces.regular == nullptr ||
        typefaces.bold == nullptr || typefaces.heading == nullptr || typefaces.code == nullptr) {
        result.diagnostic = "complex text shaping requires the complete document font context";
        return result;
    }
    if (paragraph.utf8.size() > kMaximumShapingParagraphBytes) {
        result.diagnostic = "complex text shaping paragraph exceeds the safety limit";
        return result;
    }

    if (paragraph.utf8.empty()) {
        ShapingSpan fallbackSpan;
        SkFont font = ConfigureSpanFont(fallbackSpan, typefaces, options);
        const float lineHeight = options.fallbackLineHeight > 0.0f
            ? options.fallbackLineHeight
            : std::max(font.getSpacing(), 1.0f);
        LineLayout line;
        line.y = options.startY;
        line.height = lineHeight;
        line.direction = paragraph.direction;
        line.textStart = paragraph.logicalStart;
        result.lines.push_back(std::move(line));
        result.totalHeight = lineHeight;
        result.success = true;
        result.diagnostic = "complex text shaping completed for an empty paragraph";
        return result;
    }

    if (paragraph.spans.empty()) {
        result.diagnostic = "complex text shaping paragraph has text without semantic spans";
        return result;
    }

    size_t coveredBytes = 0;
    for (const ShapingSpan& span : paragraph.spans) {
        if (span.shapingStart != coveredBytes || span.shapingLength == 0 ||
            span.shapingLength > paragraph.utf8.size() - coveredBytes) {
            result.diagnostic = "complex text shaping paragraph has invalid semantic span ranges";
            return result;
        }
        if (!span.IsAtomicObject() && span.logicalLength != span.shapingLength) {
            result.diagnostic = "complex text shaping text span has an invalid logical mapping";
            return result;
        }
        if (span.logicalLength > std::numeric_limits<size_t>::max() - span.logicalStart) {
            result.diagnostic = "complex text shaping span logical range overflows";
            return result;
        }
        if (span.IsAtomicObject() &&
            (!std::isfinite(span.objectWidth) || span.objectWidth <= 0.0f ||
             !std::isfinite(span.objectHeight) || span.objectHeight < 0.0f)) {
            result.diagnostic = "complex text shaping object has invalid geometry";
            return result;
        }
        coveredBytes += span.shapingLength;
    }
    if (coveredBytes != paragraph.utf8.size()) {
        result.diagnostic = "complex text shaping semantic spans do not cover the paragraph";
        return result;
    }

    StyleAwareFontRunIterator fontRuns(paragraph, typefaces, options);
    std::unique_ptr<SkShaper::BiDiRunIterator> bidi = SkShapers::unicode::BidiRunIterator(
        sk_ref_sp(runtime.Unicode()),
        paragraph.utf8.data(),
        paragraph.utf8.size(),
        paragraph.direction == ResolvedTextDirection::RightToLeft ? 1 : 0);
    std::unique_ptr<SkShaper::ScriptRunIterator> script = SkShapers::HB::ScriptRunIterator(
        paragraph.utf8.data(),
        paragraph.utf8.size());
    std::unique_ptr<SkShaper::LanguageRunIterator> language =
        SkShaper::MakeStdLanguageRunIterator(paragraph.utf8.data(), paragraph.utf8.size());
    if (!bidi || !script || !language) {
        result.diagnostic = "complex text shaping failed to create Unicode run iterators";
        return result;
    }

    ShapedLayoutRunHandler handler(paragraph, options, result);
    runtime.Shaper()->shape(
        paragraph.utf8.data(),
        paragraph.utf8.size(),
        fontRuns,
        *bidi,
        *script,
        *language,
        nullptr,
        0,
        options.wrapWidth,
        &handler);
    if (!result.diagnostic.empty()) {
        result.lines.clear();
        return result;
    }
    if (result.lines.empty()) {
        result.diagnostic = "complex text shaping produced no visual lines";
        return result;
    }
    result.totalHeight = handler.TotalHeight();
    result.success = true;
    result.diagnostic = "complex text shaping completed";
    return result;
}

} // namespace mdviewer
