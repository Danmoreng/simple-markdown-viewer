#include "util/utf8.h"

namespace mdviewer {
namespace {

constexpr char kReplacementCharacter[] = "\xEF\xBF\xBD";

bool IsContinuationByte(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

void AppendReplacement(Utf8SanitizationResult& result) {
    result.text.append(kReplacementCharacter);
    result.replacementCount += 1;
}

} // namespace

Utf8SanitizationResult SanitizeUtf8(std::string_view input) {
    Utf8SanitizationResult result;
    result.text.reserve(input.size());

    for (size_t index = 0; index < input.size();) {
        const unsigned char lead = static_cast<unsigned char>(input[index]);
        if (lead <= 0x7F) {
            result.text.push_back(static_cast<char>(lead));
            index += 1;
            continue;
        }

        if (lead >= 0xC2 && lead <= 0xDF) {
            if (index + 1 < input.size() && IsContinuationByte(static_cast<unsigned char>(input[index + 1]))) {
                result.text.append(input.substr(index, 2));
                index += 2;
                continue;
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if (lead == 0xE0) {
            if (index + 2 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                if (b1 >= 0xA0 && b1 <= 0xBF && IsContinuationByte(b2)) {
                    result.text.append(input.substr(index, 3));
                    index += 3;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if ((lead >= 0xE1 && lead <= 0xEC) || (lead >= 0xEE && lead <= 0xEF)) {
            if (index + 2 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                if (IsContinuationByte(b1) && IsContinuationByte(b2)) {
                    result.text.append(input.substr(index, 3));
                    index += 3;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if (lead == 0xED) {
            if (index + 2 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                if (b1 >= 0x80 && b1 <= 0x9F && IsContinuationByte(b2)) {
                    result.text.append(input.substr(index, 3));
                    index += 3;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if (lead == 0xF0) {
            if (index + 3 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                const unsigned char b3 = static_cast<unsigned char>(input[index + 3]);
                if (b1 >= 0x90 && b1 <= 0xBF && IsContinuationByte(b2) && IsContinuationByte(b3)) {
                    result.text.append(input.substr(index, 4));
                    index += 4;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if (lead >= 0xF1 && lead <= 0xF3) {
            if (index + 3 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                const unsigned char b3 = static_cast<unsigned char>(input[index + 3]);
                if (IsContinuationByte(b1) && IsContinuationByte(b2) && IsContinuationByte(b3)) {
                    result.text.append(input.substr(index, 4));
                    index += 4;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        if (lead == 0xF4) {
            if (index + 3 < input.size()) {
                const unsigned char b1 = static_cast<unsigned char>(input[index + 1]);
                const unsigned char b2 = static_cast<unsigned char>(input[index + 2]);
                const unsigned char b3 = static_cast<unsigned char>(input[index + 3]);
                if (b1 >= 0x80 && b1 <= 0x8F && IsContinuationByte(b2) && IsContinuationByte(b3)) {
                    result.text.append(input.substr(index, 4));
                    index += 4;
                    continue;
                }
            }
            AppendReplacement(result);
            index += 1;
            continue;
        }

        AppendReplacement(result);
        index += 1;
    }

    return result;
}

} // namespace mdviewer
