#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "layout/document_model.h"

namespace mdviewer::syntax {

enum class HighlightStatus {
    NotRequested,
    Highlighted,
    NoHighlights,
    UnsupportedLanguage,
    TimedOut,
    Failed,
};

struct HighlightOptions {
    std::chrono::milliseconds timeBudget{250};
    bool useCache = true;
};

struct HighlightResult {
    std::vector<InlineRun> runs;
    HighlightStatus status = HighlightStatus::NotRequested;
};

struct HighlightCacheStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t entries = 0;
};

HighlightResult HighlightCodeBlock(
    const std::string& language,
    const std::vector<InlineRun>& runs,
    const HighlightOptions& options = {});

HighlightCacheStats GetHighlightCacheStats();
void ClearHighlightCache();

} // namespace mdviewer::syntax
