#pragma once

#include <string>
#include <string_view>

namespace mdviewer {

// Adapt the pinned Mermaid emitter's SVG to Skia's supported SVG subset.
// In particular, expand arrow markers into ordinary transformed SVG paths.
bool NormalizeMermaidSvg(std::string_view source, std::string& normalized, std::string& error,
    float intrinsicWidth = 0.0f, float intrinsicHeight = 0.0f);

} // namespace mdviewer
