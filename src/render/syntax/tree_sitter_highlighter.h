#pragma once

#include <string>
#include <vector>

#include "layout/document_model.h"

namespace mdviewer::syntax {

std::vector<InlineRun> HighlightCodeBlock(const std::string& language, const std::vector<InlineRun>& runs);

} // namespace mdviewer::syntax
