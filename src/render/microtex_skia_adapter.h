#pragma once

#include <memory>

class SkCanvas;

namespace tex {
class Graphics2D;
}

namespace mdviewer {

// Creates the MicroTeX drawing bridge without exposing its platform adapter in
// the shared document model.
std::shared_ptr<tex::Graphics2D> CreateMicroTeXSkiaGraphics(SkCanvas* canvas);

} // namespace mdviewer
