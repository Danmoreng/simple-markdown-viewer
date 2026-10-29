#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace mdviewer::win {

// Layout, hit testing and reader zoom use 96-DPI logical units. Only native
// surfaces, window placement and popup screen positions use physical pixels.
inline float DpiScale(unsigned int dpi) {
    return dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
}

inline float PixelsToLogical(float pixels, float scale) { return pixels / scale; }
inline int LogicalToPixels(float logical, float scale) {
    return static_cast<int>(std::lround(logical * scale));
}
inline int LogicalCoordinate(int pixels, float scale) {
    return static_cast<int>(std::lround(PixelsToLogical(static_cast<float>(pixels), scale)));
}

inline bool LayoutWidthChanged(float previous, float current) {
    return previous < 0.0f || std::abs(previous - current) > 0.01f;
}

class Utf16InputDecoder {
public:
    void Reset() { highSurrogate_ = 0; }

    std::u16string Push(char16_t unit) {
        std::u16string result;
        if (highSurrogate_ != 0) {
            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                result.push_back(highSurrogate_);
                result.push_back(unit);
                highSurrogate_ = 0;
                return result;
            }
            result.push_back(u'\uFFFD');
            highSurrogate_ = 0;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            highSurrogate_ = unit;
        } else {
            result.push_back(unit >= 0xDC00 && unit <= 0xDFFF ? u'\uFFFD' : unit);
        }
        return result;
    }

private:
    char16_t highSurrogate_ = 0;
};

} // namespace mdviewer::win
