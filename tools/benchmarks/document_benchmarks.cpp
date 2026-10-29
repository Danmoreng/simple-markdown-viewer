#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "util/skia_font_utils.h"
#include "view/document_hit_test.h"

namespace {
using Clock = std::chrono::steady_clock;

std::string MakeDocument(size_t paragraphs) {
    std::string source;
    source.reserve(paragraphs * 300);
    for (size_t index = 0; index < paragraphs; ++index) {
        source += "Paragraph " + std::to_string(index) +
            ": A document reader wraps ordinary prose, **emphasis**, and "
            "[local links](readme.md) while preserving selection geometry. "
            "This sentence exercises several lines at a normal reading width.\n\n";
    }
    return source;
}

struct Sample {
    double layoutSeconds = 0;
    double hitTestSeconds = 0;
    size_t bytes = 0;
};

Sample Measure(size_t paragraphs, const mdviewer::DocumentTypefaceSet& faces) {
    const auto source = MakeDocument(paragraphs);
    const auto start = Clock::now();
    const auto document = mdviewer::MarkdownParser::Parse(source);
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(document, 800, faces, 17);
    const auto finished = Clock::now();
    if (layout.blocks.size() != paragraphs || layout.plainText.empty()) {
        throw std::runtime_error("Benchmark document did not produce the expected layout");
    }
    size_t validHits = 0;
    const auto hitStart = Clock::now();
    for (size_t index = 0; index < 2000; ++index) {
        const auto hit = mdviewer::HitTestDocument(layout,
            layout.totalHeight * static_cast<float>(index % 100) / 100.0f,
            0, 200, 100, {.includeTableData = false});
        if (hit.valid) ++validHits;
    }
    if (validHits == 0) throw std::runtime_error("Benchmark did not exercise hit testing");
    return {std::chrono::duration<double>(finished - start).count(),
        std::chrono::duration<double>(Clock::now() - hitStart).count(), source.size()};
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool check = argc > 1 && std::string(argv[1]) == "--check";
        const auto manager = mdviewer::CreateFontManager();
        const auto face = mdviewer::CreateDefaultTypeface(manager);
        if (!manager || !face) throw std::runtime_error("No document font is available");
        const mdviewer::DocumentTypefaceSet faces{manager.get(), face.get(), face.get(), face.get(), face.get()};
        // Warm font discovery and shaping before timing either document size.
        (void)Measure(10, faces);
        std::vector<double> smallTimes;
        std::vector<double> largeTimes;
        Sample large;
        for (int run = 0; run < 3; ++run) {
            smallTimes.push_back(Measure(250, faces).layoutSeconds);
            large = Measure(1000, faces);
            largeTimes.push_back(large.layoutSeconds);
        }
        std::sort(smallTimes.begin(), smallTimes.end());
        std::sort(largeTimes.begin(), largeTimes.end());
        const double ratio = largeTimes[1] / std::max(0.02, smallTimes[1]);
        std::cout << std::fixed << std::setprecision(4)
            << "parse+layout 250 paragraphs: " << smallTimes[1] << " s\n"
            << "parse+layout 1000 paragraphs (" << large.bytes << " bytes): " << largeTimes[1] << " s\n"
            << "4x document scaling ratio: " << ratio << "\n"
            << "2000 hit tests in large document: " << large.hitTestSeconds << " s\n";
        // Loose gross-regression budgets tolerate shared CI runners. The ratio
        // detects near-quadratic document scaling; this is not a microbenchmark.
        if (check && (largeTimes[1] > 20.0 || ratio > 12.0)) {
            std::cerr << "Document performance regression budget exceeded\n";
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
