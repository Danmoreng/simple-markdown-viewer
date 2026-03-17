#pragma once
#include <string>
#include "layout/document_model.h"

namespace mdviewer {

class MarkdownParser {
public:
    static DocumentModel Parse(const std::string& source);
};

} // namespace mdviewer
