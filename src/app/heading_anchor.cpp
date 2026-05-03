#include "app/heading_anchor.h"

#include <cctype>

namespace mdviewer {

std::string MakeHeadingAnchor(std::string_view text) {
    std::string slug;
    bool lastWasHyphen = false;
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            lastWasHyphen = false;
        } else if (std::isspace(ch) || ch == '-') {
            if (!slug.empty() && !lastWasHyphen) {
                slug.push_back('-');
                lastWasHyphen = true;
            }
        }
    }

    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

} // namespace mdviewer
