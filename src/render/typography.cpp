#include "render/typography.h"

#include <algorithm>

namespace mdviewer {

namespace {

constexpr float kMinBaseFontSize = 10.0f;
constexpr float kMaxBaseFontSize = 32.0f;

float ScaleFromBase(float value, float baseFontSize) {
    return value * (ClampBaseFontSize(baseFontSize) / kDefaultBaseFontSize);
}

} // namespace

float ClampBaseFontSize(float baseFontSize) {
    return std::clamp(baseFontSize, kMinBaseFontSize, kMaxBaseFontSize);
}

float GetBlockFontSize(BlockType blockType, float baseFontSize) {
    switch (blockType) {
        case BlockType::Heading1: return ScaleFromBase(34.0f, baseFontSize);
        case BlockType::Heading2: return ScaleFromBase(28.0f, baseFontSize);
        case BlockType::Heading3: return ScaleFromBase(22.0f, baseFontSize);
        case BlockType::Heading4: return ScaleFromBase(19.0f, baseFontSize);
        case BlockType::Heading5: return ScaleFromBase(17.0f, baseFontSize);
        case BlockType::Heading6: return ScaleFromBase(16.0f, baseFontSize);
        case BlockType::CodeBlock: return ScaleFromBase(15.0f, baseFontSize);
        default: return ClampBaseFontSize(baseFontSize);
    }
}

float GetTopMenuFontSize(float baseFontSize) {
    return ScaleFromBase(17.5f, baseFontSize);
}

float GetEmptyStateFontSize(float baseFontSize) {
    return ScaleFromBase(20.0f, baseFontSize);
}

float GetHoverOverlayFontSize(float baseFontSize) {
    return ScaleFromBase(14.0f, baseFontSize);
}

float GetCopiedOverlayFontSize(float baseFontSize) {
    return ScaleFromBase(15.0f, baseFontSize);
}

} // namespace mdviewer
