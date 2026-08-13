#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "layout/layout_engine.h"

namespace mdviewer {

class ComplexTextRuntime;

inline constexpr size_t kMaximumShapingParagraphBytes = 1024 * 1024;

struct ShapedTextInputRun {
    InlineRun run;
    size_t semanticSpanId = 0;
    float objectWidth = 0.0f;
    float objectHeight = 0.0f;
    MathLayout mathLayout;
};

struct ShapingSpan {
    size_t shapingStart = 0;
    size_t shapingLength = 0;
    size_t logicalStart = 0;
    size_t logicalLength = 0;
    size_t semanticSpanId = 0;
    InlineFormatting formatting = InlineFormatting::None;
    InlineKind kind = InlineKind::Text;
    MetadataRunRole metadataRole = MetadataRunRole::None;
    SyntaxRole syntaxRole = SyntaxRole::None;
    std::string text;
    std::string imageSource;
    std::string mathSource;
    std::string linkTarget;
    float objectWidth = 0.0f;
    float objectHeight = 0.0f;
    bool mathDisplay = false;
    MathLayout mathLayout;

    bool IsAtomicObject() const noexcept;
};

struct ShapingParagraph {
    std::string utf8;
    std::vector<ShapingSpan> spans;
    size_t logicalStart = 0;
    size_t logicalLength = 0;
    bool endedByHardBreak = false;
    bool displayMath = false;
    ResolvedTextDirection direction = ResolvedTextDirection::LeftToRight;
};

struct ShapingParagraphSet {
    std::vector<ShapingParagraph> paragraphs;
    std::string logicalText;
    size_t logicalEnd = 0;
};

ResolvedTextDirection ResolveFirstStrongDirection(std::string_view utf8);
std::optional<ResolvedTextDirection> TryResolveFirstStrongDirection(std::string_view utf8);

ShapingParagraphSet BuildShapingParagraphs(
    const std::vector<ShapedTextInputRun>& runs,
    size_t logicalStart = 0);

struct ShapedTextLayoutOptions {
    BlockType blockType = BlockType::Paragraph;
    float baseFontSize = 17.0f;
    float fontScale = 1.0f;
    float wrapWidth = 1.0f;
    float startY = 0.0f;
    float fallbackLineHeight = 0.0f;
};

struct ShapedTextLayoutResult {
    bool success = false;
    std::string diagnostic;
    ResolvedTextDirection direction = ResolvedTextDirection::LeftToRight;
    std::vector<LineLayout> lines;
    float naturalWidth = 0.0f;
    float totalHeight = 0.0f;
};

ShapedTextLayoutResult ShapeTextParagraph(
    const ShapingParagraph& paragraph,
    const ComplexTextRuntime& runtime,
    const DocumentTypefaceSet& typefaces,
    const ShapedTextLayoutOptions& options);

} // namespace mdviewer
