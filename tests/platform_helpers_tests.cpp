#include <cmath>
#include <iostream>
#include <stdexcept>

#include "platform/win/win_coordinate_helpers.h"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

namespace mdviewer::tests {
void RunPlatformHelpersTests() {
    using namespace mdviewer::win;
    try {
        for (const unsigned int dpi : {96U, 120U, 144U, 192U, 288U}) {
            const float scale = DpiScale(dpi);
            Check(std::abs(PixelsToLogical(static_cast<float>(LogicalToPixels(42.0f, scale)), scale) - 42.0f) <= 0.5f,
                "menu height must round-trip at fractional DPI");
            Check(LogicalCoordinate(LogicalToPixels(-20.0f, scale), scale) == -20,
                "captured pointer coordinates must retain negative values");
            Check(std::abs(PixelsToLogical(960.0f * scale, scale) - 960.0f) < 0.01f,
                "same logical viewport must retain the same layout width across monitors");
        }
        Check(!LayoutWidthChanged(800.0f, 800.0f), "height-only resize must not request layout");
        Check(LayoutWidthChanged(800.0f, 801.0f), "width resize must request layout");
        Check(LayoutWidthChanged(-1.0f, 800.0f), "first viewport must request layout");
        Utf16InputDecoder input;
        Check(input.Push(0xD83D).empty(), "high surrogate must wait for its pair");
        Check(input.Push(0xDE00) == u"\U0001F600", "emoji must remain a complete UTF-16 pair");
        Check(input.Push(u'A') == u"A", "BMP text must pass through");
        Check(input.Push(0xDC00) == u"\uFFFD", "isolated low surrogate must be replaced");
        input.Push(0xD800);
        Check(input.Push(u'A') == u"\uFFFDA", "broken pair must preserve following text");
        input.Push(0xD800);
        input.Reset();
        Check(input.Push(u'A') == u"A", "reset must discard incomplete input");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        throw;
    }
}
} // namespace mdviewer::tests
