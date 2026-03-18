#pragma once

#include "layout/document_model.h"

namespace mdviewer {

constexpr float kDefaultBaseFontSize = 17.0f;

float GetBlockFontSize(BlockType blockType, float baseFontSize);
float GetTopMenuFontSize(float baseFontSize);
float GetEmptyStateFontSize(float baseFontSize);
float GetHoverOverlayFontSize(float baseFontSize);
float GetCopiedOverlayFontSize(float baseFontSize);
float ClampBaseFontSize(float baseFontSize);

} // namespace mdviewer
