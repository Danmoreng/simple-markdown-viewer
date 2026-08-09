#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/app_state.h"
#include "app/document_loader.h"
#include "app/heading_anchor.h"
#include "app/link_resolver.h"
#include "app/viewer_controller.h"
#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "render/image_cache.h"
#include "render/document_renderer.h"
#include "render/menu_renderer.h"
#include "render/pdf_exporter.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "render/typography.h"
#include "util/skia_font_utils.h"
#include "util/utf8.h"
#include "view/document_hit_test.h"
#include "view/document_interaction.h"
#include "view/document_outline.h"

#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

namespace {

namespace fs = std::filesystem;

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T, typename U>
void RequireEqual(const T& actual, const U& expected, const std::string& message) {
    if (!(actual == expected)) {
        std::ostringstream stream;
        stream << message << " (expected '" << expected << "', got '" << actual << "')";
        throw TestFailure(stream.str());
    }
}

void RequireNear(float actual, float expected, float tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream stream;
        stream << message << " (expected " << expected << ", got " << actual << ")";
        throw TestFailure(stream.str());
    }
}

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("mdviewer_tests_" + std::to_string(std::rand()));
        fs::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path& Path() const {
        return path_;
    }

private:
    fs::path path_;
};

void WriteText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    Require(output.is_open(), "could not open " + path.string() + " for writing");
    output << text;
    Require(output.good(), "could not write " + path.string());
}

std::string MergeInlineRunText(const std::vector<mdviewer::InlineRun>& runs) {
    std::string text;
    for (const auto& run : runs) {
        text += run.text;
    }
    return text;
}

std::string MergeBlockText(const mdviewer::Block& block) {
    std::string text = MergeInlineRunText(block.inlineRuns);
    for (const auto& child : block.children) {
        text += MergeBlockText(child);
    }
    return text;
}

bool HasBlockType(const mdviewer::DocumentLayout& layout, mdviewer::BlockType type) {
    return std::any_of(layout.blocks.begin(), layout.blocks.end(), [type](const mdviewer::BlockLayout& block) {
        return block.type == type;
    });
}

const mdviewer::BlockLayout& FirstBlockOfType(const mdviewer::DocumentLayout& layout, mdviewer::BlockType type) {
    const auto found = std::find_if(layout.blocks.begin(), layout.blocks.end(), [type](const mdviewer::BlockLayout& block) {
        return block.type == type;
    });
    Require(found != layout.blocks.end(), "expected block type not found");
    return *found;
}

const mdviewer::Block* FindModelBlockOfType(const std::vector<mdviewer::Block>& blocks, mdviewer::BlockType type) {
    for (const auto& block : blocks) {
        if (block.type == type) {
            return &block;
        }
        if (const auto* nested = FindModelBlockOfType(block.children, type)) {
            return nested;
        }
    }
    return nullptr;
}

const mdviewer::InlineRun* FindInlineRunContaining(
    const std::vector<mdviewer::Block>& blocks,
    const std::string& text) {
    for (const auto& block : blocks) {
        const auto run = std::find_if(block.inlineRuns.begin(), block.inlineRuns.end(), [&](const auto& candidate) {
            return candidate.text.find(text) != std::string::npos;
        });
        if (run != block.inlineRuns.end()) {
            return &*run;
        }
        if (const auto* nested = FindInlineRunContaining(block.children, text)) {
            return nested;
        }
    }
    return nullptr;
}

void ConfigParsingAndSaving() {
    TempDir temp;
    const fs::path configPath = temp.Path() / "mdviewer.ini";
    WriteText(configPath,
        "; ignored\n"
        "[other]\n"
        "theme=dark\n"
        "[app]\n"
        "theme=dark\n"
        "font_family= Example Font \n"
        "base_font_size=999\n"
        "outline_side=right\n"
        "outline_width=999\n"
        "recent_file_0=C:/docs/one.md\n"
        "recent_file_0_opened_at=1700000000\n"
        "recent_file_1=\n"
        "recent_file_2=C:/docs/two.md\n");

    const auto loaded = mdviewer::LoadAppConfig(configPath);
    Require(loaded.has_value(), "config should load");
    Require(loaded->theme == mdviewer::ThemeMode::Dark, "theme should parse");
    Require(loaded->outlineSide == mdviewer::OutlineSide::Right, "outline side should parse");
    RequireNear(loaded->outlineWidth, mdviewer::kMaxOutlineWidth, 0.001f, "outline width should clamp");
    RequireEqual(loaded->fontFamilyUtf8, std::string("Example Font"), "font family should trim");
    RequireNear(loaded->baseFontSize, mdviewer::ClampBaseFontSize(999.0f), 0.001f, "font size should clamp");
    RequireEqual(loaded->recentFiles.size(), static_cast<size_t>(2), "empty recent entries should be skipped");
    RequireEqual(loaded->recentFiles[0].pathUtf8, std::string("C:/docs/one.md"), "recent files should preserve order");
    RequireEqual(loaded->recentFiles[0].openedAtUnixSeconds, 1700000000LL, "recent opened timestamp should parse");
    RequireEqual(loaded->recentFiles[1].pathUtf8, std::string("C:/docs/two.md"), "recent files should preserve sparse index order");

    WriteText(configPath, "[app]\nbase_font_size=not-a-number\noutline_width=not-a-number\ntheme=missing\n");
    const auto invalid = mdviewer::LoadAppConfig(configPath);
    Require(invalid.has_value(), "invalid values still produce defaults");
    Require(invalid->theme == mdviewer::ThemeMode::Light, "invalid theme should fall back to light");
    RequireNear(invalid->baseFontSize, mdviewer::kDefaultBaseFontSize, 0.001f, "invalid font size should fall back");
    RequireNear(invalid->outlineWidth, mdviewer::kDefaultOutlineWidth, 0.001f, "invalid outline width should fall back");

    mdviewer::AppConfig saved;
    saved.theme = mdviewer::ThemeMode::Sepia;
    saved.outlineSide = mdviewer::OutlineSide::Right;
    saved.outlineWidth = 344.0f;
    saved.fontFamilyUtf8 = "Saved Font";
    saved.baseFontSize = 21.0f;
    saved.recentFiles = {
        {"C:/docs/a.md", 1700000100},
        {"C:/docs/b.md", 1700000200},
    };
    Require(mdviewer::SaveAppConfig(configPath, saved), "config should save");
    const auto roundTrip = mdviewer::LoadAppConfig(configPath);
    Require(roundTrip.has_value(), "saved config should reload");
    Require(roundTrip->theme == mdviewer::ThemeMode::Sepia, "saved theme should round-trip");
    Require(roundTrip->outlineSide == mdviewer::OutlineSide::Right, "saved outline side should round-trip");
    RequireNear(roundTrip->outlineWidth, saved.outlineWidth, 0.001f, "saved outline width should round-trip");
    RequireEqual(roundTrip->fontFamilyUtf8, saved.fontFamilyUtf8, "saved font should round-trip");
    RequireEqual(roundTrip->recentFiles.size(), saved.recentFiles.size(), "saved recent files should round-trip");
    RequireEqual(roundTrip->recentFiles[0].pathUtf8, saved.recentFiles[0].pathUtf8, "saved recent path should round-trip");
    RequireEqual(roundTrip->recentFiles[0].openedAtUnixSeconds, saved.recentFiles[0].openedAtUnixSeconds, "saved recent timestamp should round-trip");
}

void RecentFilesAndHistory() {
    TempDir temp;
    const fs::path configPath = temp.Path() / "mdviewer.ini";
    std::ostringstream config;
    config << "[app]\n";
    for (int index = 0; index < 10; ++index) {
        config << "recent_file_" << index << '=' << (temp.Path() / ("file" + std::to_string(index) + ".md")).string() << '\n';
        config << "recent_file_" << index << "_opened_at=" << (1700000000 + index) << '\n';
    }
    config << "recent_file_10=" << (temp.Path() / "file5.md").string() << '\n';
    WriteText(configPath, config.str());

    mdviewer::ViewerController controller;
    controller.SetConfigPath(configPath);
    Require(controller.LoadConfig(), "controller config should load");
    const auto& recent = controller.GetRecentFiles();
    RequireEqual(recent.size(), static_cast<size_t>(8), "recent files should be capped");
    Require(recent.front().path.filename() == "file0.md", "loaded recent files should keep most-recent-first order");
    RequireEqual(recent.front().openedAtUnixSeconds, 1700000000LL, "loaded recent timestamp should be preserved");
    Require(std::any_of(recent.begin(), recent.end(), [](const mdviewer::RecentFileEntry& entry) {
        return entry.path.filename() == "file5.md";
    }), "duplicate recent file later in config should not create another entry");

    const fs::path openedPath = temp.Path() / "fresh.md";
    WriteText(openedPath, "# Fresh\n");
    Require(
        controller.OpenFile(openedPath, 800.0f, nullptr, {}, {}) == mdviewer::OpenDocumentStatus::Success,
        "opening a file should succeed");
    Require(controller.GetRecentFiles().front().path.filename() == "fresh.md", "newly opened file should be first in recent files");
    Require(controller.GetRecentFiles().front().openedAtUnixSeconds > 0, "newly opened file should record an opened timestamp");

    mdviewer::AppState state;
    const fs::path first = temp.Path() / "first.md";
    const fs::path second = temp.Path() / "second.md";
    const fs::path third = temp.Path() / "third.md";
    state.PushHistory(first);
    state.PushHistory(second);
    state.PushHistory(second);
    RequireEqual(state.history.size(), static_cast<size_t>(2), "duplicate current history item should not be pushed");
    Require(state.CanGoBack(), "history should allow back");
    state.historyIndex = 0;
    Require(state.CanGoForward(), "history should allow forward");
    state.PushHistory(third);
    RequireEqual(state.history.size(), static_cast<size_t>(2), "pushing after back should discard forward history");
    RequireEqual(state.history.back(), third, "new history target should be appended");
}

void ConfigPathMigration() {
    TempDir temp;
    const fs::path canonicalPath = temp.Path() / "user" / "config" / "mdviewer.ini";
    const fs::path legacyPath = temp.Path() / "exe" / "mdviewer.ini";

    WriteText(legacyPath,
        "[app]\n"
        "theme=dark\n"
        "font_family=Legacy Font\n"
        "base_font_size=18\n");

    mdviewer::ViewerController controller;
    controller.SetConfigPath(canonicalPath);
    controller.SetLegacyConfigPath(legacyPath);
    Require(controller.LoadConfig(), "controller should load legacy config when canonical config is absent");
    Require(controller.GetTheme() == mdviewer::ThemeMode::Dark, "legacy theme should load");
    RequireEqual(controller.GetFontFamilyUtf8(), std::string("Legacy Font"), "legacy font should load");

    WriteText(canonicalPath,
        "[app]\n"
        "theme=sepia\n"
        "font_family=Canonical Font\n"
        "base_font_size=20\n");

    Require(controller.LoadConfig(), "controller should load canonical config when present");
    Require(controller.GetTheme() == mdviewer::ThemeMode::Sepia, "canonical theme should win over legacy");
    RequireEqual(controller.GetFontFamilyUtf8(), std::string("Canonical Font"), "canonical font should win over legacy");

    const fs::path nestedCanonicalPath = temp.Path() / "new-user-dir" / "nested" / "mdviewer.ini";
    mdviewer::ViewerController saveController;
    saveController.SetConfigPath(nestedCanonicalPath);
    saveController.SetTheme(mdviewer::ThemeMode::Dark);
    saveController.SetFontFamilyUtf8("Saved User Font");
    Require(saveController.SaveConfig(), "saving should create missing canonical config directories");
    Require(fs::exists(nestedCanonicalPath), "canonical config file should be created");

    const auto saved = mdviewer::LoadAppConfig(nestedCanonicalPath);
    Require(saved.has_value(), "saved canonical config should be readable");
    Require(saved->theme == mdviewer::ThemeMode::Dark, "saved canonical config should preserve theme");
    RequireEqual(saved->fontFamilyUtf8, std::string("Saved User Font"), "saved canonical config should preserve font");
}

void LinkResolution() {
    TempDir temp;
    const fs::path root = temp.Path();
    const fs::path current = root / "docs" / "index.md";
    const fs::path sibling = root / "docs" / "Other File.md";
    const fs::path child = root / "docs" / "nested" / "page.md";
    const fs::path textNoExtension = root / "docs" / "LICENSE";
    const fs::path binary = root / "docs" / "app.exe";
    const fs::path svg = root / "docs" / "diagram.svg";
    const fs::path tldraw = root / "docs" / "diagram.tldraw";
    WriteText(current, "# Home\n");
    WriteText(sibling, "# Other\n");
    WriteText(child, "# Child\n");
    WriteText(textNoExtension, "license text\n");
    WriteText(binary, std::string("MZ\0\0binary", 10));
    WriteText(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\"></svg>");
    WriteText(tldraw, std::string("PK\x03\x04\0binary", 11));

    auto target = mdviewer::ResolveLinkTarget(current, "#Section%201", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "fragment-only link should stay internal");
    RequireEqual(target.path, current, "fragment-only link should target current file");
    RequireEqual(target.fragment, std::string("Section 1"), "fragment should be percent-decoded");

    target = mdviewer::ResolveLinkTarget(current, "Other%20File.md#A%20B", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "percent-encoded markdown path should resolve internally");
    RequireEqual(target.path.lexically_normal(), sibling.lexically_normal(), "encoded spaces should resolve to sibling path");
    RequireEqual(target.fragment, std::string("A B"), "file fragment should decode");

    target = mdviewer::ResolveLinkTarget(current, "nested/page.md?ignored=true", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "relative markdown path with query should resolve");
    RequireEqual(target.path.lexically_normal(), child.lexically_normal(), "relative nested path should resolve");

    target = mdviewer::ResolveLinkTarget(current, "LICENSE", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "extensionless known text file should open internally");

    target = mdviewer::ResolveLinkTarget(current, "missing.md", false);
    Require(target.kind == mdviewer::LinkTargetKind::Invalid, "missing file should be invalid");

    target = mdviewer::ResolveLinkTarget(current, "https://example.com/a%20b#frag", false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalUrl, "https link should open externally");
    RequireEqual(target.externalUrl, std::string("https://example.com/a%20b#frag"), "external URL should be preserved");

    target = mdviewer::ResolveLinkTarget(current, "javascript:alert(1)", false);
    Require(target.kind == mdviewer::LinkTargetKind::Invalid, "unsafe unsupported scheme should be invalid");

    target = mdviewer::ResolveLinkTarget(current, binary.string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "existing binary path should open as external path");

    target = mdviewer::ResolveLinkTarget(current, svg.string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "SVG links should not be loaded as text documents");
    Require(mdviewer::LoadDocumentFromPath(svg).status == mdviewer::DocumentLoadStatus::BinaryFile,
            "opening an SVG directly should not parse it as a text document");

    target = mdviewer::ResolveLinkTarget(current, tldraw.string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "tldraw archives should open externally");

    target = mdviewer::ResolveLinkTarget(current, sibling.string(), true);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "forceExternal should override internal text handling");
}

void SvgImageRendering() {
    TempDir temp;
    const fs::path svg = temp.Path() / "diagram.svg";
    WriteText(
        svg,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\" viewBox=\"0 0 64 32\">"
        "<rect width=\"64\" height=\"32\" fill=\"#00aa55\"/></svg>");

    mdviewer::DocumentImageCache cache;
    const auto [width, height] = cache.GetImageSize(svg.filename().string(), temp.Path());
    RequireNear(width, 64.0f, 0.01f, "SVG intrinsic width should be parsed");
    RequireNear(height, 32.0f, 0.01f, "SVG intrinsic height should be parsed");

    const auto image = cache.GetImage(svg.filename().string(), temp.Path(), 320.0f, 160.0f);
    Require(image != nullptr, "SVG should rasterize through Skia");
    RequireEqual(image->width(), 320, "rasterized SVG should use requested width");
    RequireEqual(image->height(), 160, "rasterized SVG should use requested height");

    const fs::path textSvg = temp.Path() / "text.svg";
    WriteText(
        textSvg,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" height=\"40\">"
        "<text x=\"4\" y=\"30\" font-size=\"28\" fill=\"#ffffff\">Visible</text></svg>");
    const auto textImage = cache.GetImage(textSvg.filename().string(), temp.Path(), 160.0f, 40.0f);
    Require(textImage != nullptr, "standard SVG text should rasterize");
    SkPixmap textPixels;
    Require(textImage->peekPixels(&textPixels), "rasterized SVG text pixels should be readable");
    bool foundVisibleTextPixel = false;
    for (int y = 0; y < textPixels.height() && !foundVisibleTextPixel; ++y) {
        for (int x = 0; x < textPixels.width(); ++x) {
            if (SkColorGetA(*textPixels.addr32(x, y)) != 0) {
                foundVisibleTextPixel = true;
                break;
            }
        }
    }
    Require(foundVisibleTextPixel, "normal SVG <text> content should produce visible pixels");

    const auto document = mdviewer::MarkdownParser::Parse(
        "[![diagram](diagram.svg)](https://example.com/full.svg)\n");
    Require(!document.blocks.empty() && !document.blocks[0].inlineRuns.empty(),
            "linked SVG fixture should produce an image run");
    const auto& run = document.blocks[0].inlineRuns[0];
    Require(run.kind == mdviewer::InlineKind::Image, "linked SVG fixture should preserve image semantics");
    RequireEqual(run.imageSource, std::string("diagram.svg"), "image run should preserve its local SVG source");
    RequireEqual(run.linkTarget, std::string("https://example.com/full.svg"),
                 "linked image should preserve its outer activation target");
}

void HeadingAnchors() {
    RequireEqual(mdviewer::MakeHeadingAnchor("Hello, World!"), std::string("hello-world"), "punctuation should be stripped");
    RequireEqual(mdviewer::MakeHeadingAnchor("  Multiple   Spaces  "), std::string("multiple-spaces"), "spaces should collapse");
    RequireEqual(mdviewer::MakeHeadingAnchor("🚀 Launch"), std::string("launch"), "emoji should be ignored when ASCII text remains");
    RequireEqual(mdviewer::MakeHeadingAnchor("日本語"), std::string("日本語"), "Unicode-only headings should keep their text");
    RequireEqual(mdviewer::MakeHeadingAnchor("Résumé Guide"), std::string("résumé-guide"), "Latin Unicode letters should be preserved");
    RequireEqual(mdviewer::MakeHeadingAnchor("C++ & C#"), std::string("c-c"), "ASCII punctuation should be stripped");
    RequireEqual(mdviewer::MakeHeadingAnchor("Emoji ✨ Heading"), std::string("emoji-heading"), "emoji symbols should be stripped without extra hyphens");

    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(
        "# Hello\n"
        "## Hello\n"
        "# 🚀 Launch\n"
        "# 日本語\n"
        "# Résumé Guide\n");
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, nullptr, mdviewer::kDefaultBaseFontSize);
    Require(layout.anchors.contains("hello"), "first duplicate heading should use base slug");
    Require(layout.anchors.contains("hello-2"), "second duplicate heading should get numeric suffix");
    Require(layout.anchors.contains("launch"), "emoji plus ASCII heading should anchor on ASCII text");
    Require(layout.anchors.contains("日本語"), "Unicode-only heading should produce an anchor");
    Require(layout.anchors.contains("résumé-guide"), "Latin Unicode heading should produce an anchor");
    RequireEqual(layout.outline.size(), static_cast<size_t>(5), "all headings should appear in the document outline");
    RequireEqual(layout.outline[0].level, 1, "outline should preserve heading level");
    RequireEqual(layout.outline[1].slug, std::string("hello-2"), "outline should preserve unique duplicate slug");
    RequireEqual(layout.outline[3].slug, std::string("日本語"), "outline should include Unicode-only heading anchors");

    mdviewer::AppState appState;
    appState.docLayout = layout;
    RequireNear(mdviewer::GetOutlineSidebarWidth(appState), mdviewer::kOutlineSidebarWidth, 0.001f, "headings should enable outline sidebar");
    Require(mdviewer::HitTestOutlineToggle(appState, mdviewer::kOutlineSidebarWidth - 18.0f, 42.0f, 900.0f, 30.0f), "outline toggle should hit the fixed top-right button");
    const auto firstOutlineHit = mdviewer::HitTestOutlineSidebar(appState, 24.0f, 74.0f, 900.0f, 30.0f);
    Require(firstOutlineHit.has_value() && *firstOutlineHit == 0, "outline hit test should identify the first row");
    const auto outsideOutlineHit = mdviewer::HitTestOutlineSidebar(appState, mdviewer::kOutlineSidebarWidth + 1.0f, 74.0f, 900.0f, 30.0f);
    Require(!outsideOutlineHit.has_value(), "outline hit test should ignore points outside the sidebar");
    Require(mdviewer::FocusOutlineItem(appState, 1, 10000.0f), "outline item should be focusable");
    RequireEqual(appState.focusedOutlineIndex, static_cast<size_t>(1), "focused outline index should update");
    Require(mdviewer::MoveOutlineFocus(appState, 1, 10000.0f), "outline focus should move down");
    RequireEqual(appState.focusedOutlineIndex, static_cast<size_t>(2), "outline focus should move to the next item");
    appState.outlineCollapsed = true;
    RequireNear(mdviewer::GetOutlineSidebarWidth(appState), mdviewer::kOutlineCollapsedWidth, 0.001f, "collapsed outline should keep a narrow rail");
    Require(!mdviewer::HitTestOutlineSidebar(appState, 24.0f, 74.0f, 900.0f, 30.0f).has_value(), "collapsed outline should not hit rows");
    appState.outlineCollapsed = false;
    appState.outlineSide = mdviewer::OutlineSide::Right;
    RequireNear(mdviewer::GetOutlineX(appState, 900.0f), 900.0f - mdviewer::kOutlineSidebarWidth, 0.001f, "right outline should be placed at the right edge");
    Require(mdviewer::HitTestOutlineToggle(appState, 900.0f - mdviewer::kOutlineSidebarWidth + 18.0f, 42.0f, 900.0f, 30.0f), "right outline toggle should hit at the inner left edge");
    Require(mdviewer::HitTestOutlineSidebar(appState, 900.0f - mdviewer::kOutlineSidebarWidth + 24.0f, 74.0f, 900.0f, 30.0f).has_value(), "right outline rows should hit inside the right sidebar");

    appState.outlineSide = mdviewer::OutlineSide::Left;
    appState.outlineWidth = 340.0f;
    RequireNear(mdviewer::GetOutlineSidebarWidth(appState), 340.0f, 0.001f, "expanded outline should use the configured width");
    Require(mdviewer::HitTestOutlineResizeHandle(appState, 340.0f, 180.0f, 900.0f, 600.0f, 30.0f), "outline divider should expose a resize handle");
    Require(mdviewer::ResizeOutlineSidebar(appState, 410.0f, 900.0f), "dragging the divider should update outline width");
    RequireNear(appState.outlineWidth, 410.0f, 0.001f, "outline resize should preserve the requested width");

    mdviewer::AppState longOutlineState;
    for (size_t index = 0; index < 40; ++index) {
        longOutlineState.docLayout.outline.push_back({
            1,
            "Heading " + std::to_string(index),
            "heading-" + std::to_string(index),
            static_cast<float>(index) * 100.0f,
        });
    }
    longOutlineState.scrollOffset = 3000.0f;
    mdviewer::SyncOutlineScrollToDocument(longOutlineState, 300.0f, 30.0f);
    Require(longOutlineState.outlineScrollOffset > 0.0f, "outline should follow the active heading in a long document");
    const auto initialThumb = mdviewer::GetOutlineScrollbarThumbRect(longOutlineState, 900.0f, 300.0f, 30.0f);
    Require(initialThumb.has_value(), "overflowing outline should expose a scrollbar thumb");
    const auto visibleOutlineHit = mdviewer::HitTestOutlineSidebar(longOutlineState, 24.0f, 74.0f, 900.0f, 30.0f);
    const size_t expectedVisibleIndex = static_cast<size_t>(longOutlineState.outlineScrollOffset / mdviewer::kOutlineItemHeight);
    Require(
        visibleOutlineHit.has_value() && *visibleOutlineHit == expectedVisibleIndex,
        "outline hit testing should account for its scroll offset");

    const float followedOffset = longOutlineState.outlineScrollOffset;
    Require(mdviewer::ScrollOutlineBy(longOutlineState, -64.0f, 300.0f, 30.0f), "outline wheel scrolling should move its own viewport");
    const float manualOffset = longOutlineState.outlineScrollOffset;
    Require(manualOffset > followedOffset, "negative wheel delta should move farther down the outline");
    mdviewer::SyncOutlineScrollToDocument(longOutlineState, 300.0f, 30.0f);
    RequireNear(longOutlineState.outlineScrollOffset, manualOffset, 0.001f, "manual outline scroll should remain until the document moves again");

    longOutlineState.scrollOffset = 100.0f;
    mdviewer::SyncOutlineScrollToDocument(longOutlineState, 300.0f, 30.0f);
    Require(longOutlineState.outlineScrollOffset < manualOffset, "document scrolling should bring its new active heading into the outline viewport");

    const auto thumb = mdviewer::GetOutlineScrollbarThumbRect(longOutlineState, 900.0f, 300.0f, 30.0f);
    Require(thumb.has_value(), "outline scrollbar should remain available after following the document");
    mdviewer::BeginOutlineScrollbarDrag(longOutlineState, thumb->height() * 0.5f);
    Require(mdviewer::UpdateOutlineScrollFromThumb(
        longOutlineState,
        300.0f - mdviewer::kOutlineBottomPadding - (thumb->height() * 0.5f),
        300.0f,
        30.0f), "dragging the outline scrollbar should update its offset");
    RequireNear(
        longOutlineState.outlineScrollOffset,
        mdviewer::GetMaxOutlineScroll(longOutlineState, 300.0f, 30.0f),
        0.01f,
        "dragging the thumb to the bottom should reach the final outline items");
    mdviewer::EndOutlinePointerDrag(longOutlineState);
    Require(!longOutlineState.isDraggingOutlineScrollbar, "releasing the outline thumb should end its drag state");
}

void HitTestingMeasuresOnlyClosestLine() {
    mdviewer::DocumentLayout layout{};
    mdviewer::BlockLayout block{};
    block.type = mdviewer::BlockType::Paragraph;
    for (size_t index = 0; index < 200; ++index) {
        mdviewer::LineLayout line{};
        line.x = 10.0f;
        line.y = static_cast<float>(index) * 20.0f;
        line.height = 18.0f;
        line.textStart = index * 4;
        line.textLength = 4;
        line.runs.push_back(mdviewer::RunLayout{
            .text = "text",
            .textStart = line.textStart,
        });
        block.lines.push_back(std::move(line));
    }
    layout.blocks.push_back(std::move(block));

    int widthCalls = 0;
    int positionCalls = 0;
    mdviewer::HitTestCallbacks callbacks;
    callbacks.get_run_visual_width = [&](const auto&, const auto&, const auto&) {
        ++widthCalls;
        return 40.0f;
    };
    callbacks.find_text_position_in_run = [&](const auto&, const auto&, const auto& run, float) {
        ++positionCalls;
        return run.textStart + 2;
    };

    const auto hit = mdviewer::HitTestDocument(layout, 0.0f, 30.0f, 20.0f, 2035.0f, callbacks);
    Require(hit.valid, "hit testing should find the closest visible line");
    RequireEqual(hit.position, static_cast<size_t>(402), "hit testing should use the target line position");
    RequireEqual(widthCalls, 1, "hit testing should measure runs on only the closest line");
    RequireEqual(positionCalls, 1, "hit testing should resolve one text position");
}

void Utf8Boundaries() {
    const std::string text = std::string("A") + "\xE2\x89\x88" + "B";
    RequireEqual(mdviewer::NextUtf8Boundary(text, 0), static_cast<size_t>(1),
                 "ASCII should advance by one byte");
    RequireEqual(mdviewer::NextUtf8Boundary(text, 1), static_cast<size_t>(4),
                 "UTF-8 hit testing must skip continuation bytes");
    RequireEqual(mdviewer::NextUtf8Boundary(text, 4), static_cast<size_t>(5),
                 "the byte after a multibyte code point should remain addressable");
}

void MarkdownSafetyLimits() {
    std::string deeplyNested;
    for (size_t depth = 0; depth < 300; ++depth) {
        deeplyNested += "> ";
    }
    deeplyNested += "content\n";

    const auto document = mdviewer::MarkdownParser::Parse(deeplyNested);
    Require(document.blocks.empty(), "Markdown beyond the nesting limit should fail without a partial model");
}

void DocumentSizeLimit() {
    TempDir temp;
    const fs::path oversizedPath = temp.Path() / "oversized.md";
    std::ofstream output(oversizedPath, std::ios::binary);
    Require(output.is_open(), "oversized fixture should open");
    output.seekp(static_cast<std::streamoff>(mdviewer::kMaxDocumentFileSizeBytes));
    output.put('\n');
    output.close();

    const auto result = mdviewer::LoadDocumentFromPath(oversizedPath);
    Require(result.status == mdviewer::DocumentLoadStatus::FileTooLarge,
            "documents above the hard size limit should be rejected before reading");
}

void MarkdownCorrectnessFoundation() {
    const mdviewer::DocumentModel fenced = mdviewer::MarkdownParser::Parse(
        "- Parent item\n\n"
        "    ```cpp\n"
        "    #include <vector>\n"
        "    *pointer = value;\n"
        "    >literal\n"
        "    ```\n");
    const mdviewer::Block* codeBlock = FindModelBlockOfType(fenced.blocks, mdviewer::BlockType::CodeBlock);
    Require(codeBlock != nullptr, "nested list fixture should retain its fenced code block");
    const std::string codeText = MergeInlineRunText(codeBlock->inlineRuns);
    Require(codeText.find("#include <vector>") != std::string::npos, "parser must preserve hash-prefixed fenced code exactly");
    Require(codeText.find("*pointer = value;") != std::string::npos, "parser must preserve list-like fenced code exactly");
    Require(codeText.find(">literal") != std::string::npos, "parser must preserve quote-like fenced code exactly");
    Require(codeText.find("# include") == std::string::npos, "parser must not normalize fenced code contents");

    const mdviewer::DocumentModel nestedStyles = mdviewer::MarkdownParser::Parse(
        "***bold italic*** and [**bold link**](https://example.com) and ~~**bold strike**~~\n");
    const auto* boldItalic = FindInlineRunContaining(nestedStyles.blocks, "bold italic");
    Require(boldItalic != nullptr, "nested emphasis fixture should produce a text run");
    Require(mdviewer::HasFormatting(boldItalic->formatting, mdviewer::InlineFormatting::Strong), "bold+italic run should retain strong formatting");
    Require(mdviewer::HasFormatting(boldItalic->formatting, mdviewer::InlineFormatting::Emphasis), "bold+italic run should retain emphasis formatting");

    const auto* boldLink = FindInlineRunContaining(nestedStyles.blocks, "bold link");
    Require(boldLink != nullptr, "bold link fixture should produce a text run");
    Require(mdviewer::HasFormatting(boldLink->formatting, mdviewer::InlineFormatting::Strong), "link text should retain strong formatting");
    RequireEqual(boldLink->linkTarget, std::string("https://example.com"), "formatting should not replace the link target");

    const auto* boldStrike = FindInlineRunContaining(nestedStyles.blocks, "bold strike");
    Require(boldStrike != nullptr, "bold strike fixture should produce a text run");
    Require(mdviewer::HasFormatting(boldStrike->formatting, mdviewer::InlineFormatting::Strong), "struck text should retain strong formatting");
    Require(mdviewer::HasFormatting(boldStrike->formatting, mdviewer::InlineFormatting::Strikethrough), "strong text should retain strikethrough formatting");

    const mdviewer::DocumentModel breaks = mdviewer::MarkdownParser::Parse(
        "soft line\ncontinues here\n\n"
        "hard line  \nbreaks here\n");
    size_t softBreakCount = 0;
    size_t hardBreakCount = 0;
    for (const auto& block : breaks.blocks) {
        for (const auto& run : block.inlineRuns) {
            softBreakCount += run.kind == mdviewer::InlineKind::SoftBreak ? 1U : 0U;
            hardBreakCount += run.kind == mdviewer::InlineKind::HardBreak ? 1U : 0U;
        }
    }
    RequireEqual(softBreakCount, static_cast<size_t>(1), "soft break should remain distinct in the document model");
    RequireEqual(hardBreakCount, static_cast<size_t>(1), "hard break should remain distinct in the document model");

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regularTypeface = mdviewer::CreateDefaultTypeface(fontMgr);
    const sk_sp<SkTypeface> boldTypeface = mdviewer::CreateDefaultTypeface(fontMgr, SkFontStyle::Bold());
    const auto breakLayout = mdviewer::LayoutEngine::ComputeLayout(
        breaks,
        1200.0f,
        regularTypeface.get(),
        mdviewer::kDefaultBaseFontSize);
    Require(breakLayout.blocks.size() >= 2, "break fixture should produce two paragraphs");
    RequireEqual(breakLayout.blocks[0].lines.size(), static_cast<size_t>(1), "soft break should render as normal whitespace on a wide line");
    Require(breakLayout.blocks[1].lines.size() >= 2, "hard break should force a visible new line");
    Require(breakLayout.plainText.find("soft line continues here") != std::string::npos, "copy/search text should represent a soft break as a space");
    Require(breakLayout.plainText.find("hard line\nbreaks here") != std::string::npos, "copy/search text should preserve a hard break as a newline");

    mdviewer::AppState searchState;
    searchState.docLayout = breakLayout;
    mdviewer::OpenSearch(searchState);
    mdviewer::InsertSearchText(searchState, "soft line continues here");
    RequireEqual(searchState.searchMatches.size(), static_cast<size_t>(1), "search should match across a rendered soft break");

    mdviewer::DocumentTypefaceSet typefaces;
    typefaces.fontMgr = fontMgr.get();
    typefaces.regular = regularTypeface.get();
    typefaces.bold = boldTypeface.get();
    typefaces.heading = boldTypeface.get();
    typefaces.code = regularTypeface.get();
    SkFont combinedFont;
    mdviewer::ConfigureDocumentFont(
        combinedFont,
        typefaces,
        mdviewer::BlockType::Paragraph,
        mdviewer::InlineFormatting::Strong | mdviewer::InlineFormatting::Emphasis,
        mdviewer::kDefaultBaseFontSize);
    Require(combinedFont.getTypeface() == boldTypeface.get(), "renderer should select the bold face for combined strong+emphasis text");
    Require(combinedFont.getSkewX() < 0.0f, "renderer should also retain emphasis skew on strong text");

    const auto linkLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse("[**bold link**](https://example.com)\n"),
        600.0f,
        regularTypeface.get(),
        mdviewer::kDefaultBaseFontSize);
    Require(!linkLayout.blocks.empty() && !linkLayout.blocks[0].lines.empty(), "link fixture should create a laid-out line");
    mdviewer::HitTestCallbacks callbacks;
    callbacks.get_run_visual_width = [](const auto&, const auto&, const auto&) { return 120.0f; };
    callbacks.find_text_position_in_run = [](const auto&, const auto&, const auto& run, float) { return run.textStart; };
    const auto linkHit = mdviewer::HitTestDocument(
        linkLayout,
        0.0f,
        30.0f,
        linkLayout.blocks[0].lines[0].x + 1.0f,
        linkLayout.blocks[0].lines[0].y + 31.0f,
        callbacks);
    RequireEqual(linkHit.url, std::string("https://example.com"), "hit testing should preserve a formatted link target");
    Require(mdviewer::HasFormatting(linkHit.formatting, mdviewer::InlineFormatting::Strong), "hit testing should preserve combined formatting metadata");
}

void SafeHtmlSubset() {
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(
        "<p align=\"center\">\n"
        "  <img src=\"docs/logo.svg\" alt=\"project logo\" width=\"160\">\n"
        "</p>\n\n"
        "<h1 align=\"center\">Project &amp; Tools</h1>\n\n"
        "<p align=\"center\">First line.<br>Second line.</p>\n\n"
        "<p>Text before <a href=\"target.md\">linked words</a> and after.</p>\n\n"
        "<p align=\"center\">\n"
        "  <a href=\"LICENSE\"><img src=\"https://img.shields.io/license.svg\" alt=\"Apache 2.0\"></a>\n"
        "</p>\n");

    RequireEqual(document.blocks.size(), static_cast<size_t>(5), "supported HTML blocks should become five native blocks");
    Require(document.blocks[0].type == mdviewer::BlockType::Paragraph, "HTML p should become a paragraph");
    Require(document.blocks[0].align == mdviewer::TextAlign::Center, "HTML align=center should be preserved");
    RequireEqual(document.blocks[0].inlineRuns.size(), static_cast<size_t>(1), "image-only HTML paragraph should not retain indentation whitespace");
    const auto& logo = document.blocks[0].inlineRuns[0];
    Require(logo.kind == mdviewer::InlineKind::Image, "HTML img should become a native image run");
    RequireEqual(logo.imageSource, std::string("docs/logo.svg"), "HTML image source should be preserved");
    RequireNear(logo.imageRequestedWidth, 160.0f, 0.001f, "HTML image width should be preserved");

    Require(document.blocks[1].type == mdviewer::BlockType::Heading1, "HTML h1 should become a native heading");
    RequireEqual(MergeInlineRunText(document.blocks[1].inlineRuns), std::string("Project & Tools"), "HTML entities should decode");
    const auto hardBreak = std::find_if(document.blocks[2].inlineRuns.begin(), document.blocks[2].inlineRuns.end(), [](const auto& run) {
        return run.kind == mdviewer::InlineKind::HardBreak;
    });
    Require(hardBreak != document.blocks[2].inlineRuns.end(), "HTML br should become a hard break");

    RequireEqual(MergeInlineRunText(document.blocks[3].inlineRuns),
                 std::string("Text before linked words and after."),
                 "HTML whitespace around inline links should remain readable");
    const auto& badge = document.blocks[4].inlineRuns[0];
    Require(badge.kind == mdviewer::InlineKind::Image, "linked HTML badge should remain an image");
    RequireEqual(badge.linkTarget, std::string("LICENSE"), "HTML image should retain its enclosing link");

    const mdviewer::DocumentModel unsafe = mdviewer::MarkdownParser::Parse(
        "<script>alert('not executed')</script>\n");
    RequireEqual(unsafe.blocks.size(), static_cast<size_t>(1), "unsupported HTML should remain one source block");
    Require(unsafe.blocks[0].type == mdviewer::BlockType::RawHtml, "unsafe HTML should not become a renderable HTML element");
    Require(MergeInlineRunText(unsafe.blocks[0].inlineRuns).find("<script>") != std::string::npos,
            "unsafe HTML should remain visible as source");

    const mdviewer::DocumentModel inlineBreak = mdviewer::MarkdownParser::Parse("before<br>after\n");
    RequireEqual(inlineBreak.blocks.size(), static_cast<size_t>(1), "inline HTML br should stay in its paragraph");
    Require(std::any_of(inlineBreak.blocks[0].inlineRuns.begin(), inlineBreak.blocks[0].inlineRuns.end(), [](const auto& run) {
        return run.kind == mdviewer::InlineKind::HardBreak;
    }), "inline HTML br should become a hard break");

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(
        document,
        900.0f,
        typeface.get(),
        17.0f,
        [](const std::string& source) {
            return source == "docs/logo.svg"
                ? std::pair<float, float>{256.0f, 256.0f}
                : std::pair<float, float>{0.0f, 0.0f};
        });
    RequireNear(layout.blocks[0].lines[0].runs[0].imageWidth, 160.0f, 0.5f, "requested HTML image width should drive layout");
    RequireNear(layout.blocks[0].lines[0].runs[0].imageHeight, 160.0f, 0.5f, "one requested image dimension should preserve intrinsic aspect ratio");
    Require(layout.blocks[0].lines[0].x > layout.blocks[0].bounds.left(), "centered HTML image should be centered by native layout");
    Require(layout.blocks[2].lines.size() >= 2, "HTML br should force a second rendered line");
}

void GithubAlerts() {
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(
        "> [!NOTE]\n> Note body.\n\n"
        "> [!TIP]\n> Tip body.\n\n"
        "> [!IMPORTANT]\n> Important body.\n\n"
        "> [!WARNING]\n> Warning body.\n\n"
        "> [!CAUTION]\n> Caution body.\n\n"
        "> Ordinary quotation.\n");
    const std::vector<mdviewer::AlertKind> expected = {
        mdviewer::AlertKind::Note,
        mdviewer::AlertKind::Tip,
        mdviewer::AlertKind::Important,
        mdviewer::AlertKind::Warning,
        mdviewer::AlertKind::Caution,
    };
    RequireEqual(document.blocks.size(), static_cast<size_t>(6), "five alerts and one ordinary quote should remain separate blocks");
    for (size_t index = 0; index < expected.size(); ++index) {
        const auto& block = document.blocks[index];
        Require(block.type == mdviewer::BlockType::Blockquote, "GitHub alert should retain blockquote structure");
        Require(block.alertKind == expected[index], "GitHub alert marker should map to its native alert kind");
        const std::string text = MergeBlockText(block);
        Require(text.find("[!") == std::string::npos, "visual alert marker should be removed from document content");
        Require(text.find("body.") != std::string::npos, "alert body should remain intact");
    }
    Require(document.blocks.back().alertKind == mdviewer::AlertKind::None,
            "ordinary blockquotes should keep ordinary blockquote styling");

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(document, 680.0f, typeface.get(), 17.0f);
    RequireEqual(layout.blocks.size(), document.blocks.size(), "every alert should produce a layout block");
    Require(layout.blocks[2].alertKind == mdviewer::AlertKind::Important,
            "important styling should survive into the renderer model");
    Require(!layout.blocks[2].children.empty(), "important alert should retain its content layout");
    Require(layout.blocks[2].children.front().bounds.top() > layout.blocks[2].bounds.top() + 16.0f,
            "alert layout should reserve a title row for icon and label");

    mdviewer::AppState renderState;
    renderState.docLayout = layout;
    renderState.outlineCollapsed = true;
    const int height = static_cast<int>(std::ceil(layout.totalHeight + 20.0f));
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(680, height));
    Require(surface != nullptr, "alert rendering regression should create a raster surface");
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = surface->getCanvas(),
        .appState = &renderState,
        .palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark),
        .typefaces = mdviewer::DocumentTypefaceSet{
            .fontMgr = fontMgr.get(),
            .regular = typeface.get(),
            .bold = typeface.get(),
            .heading = typeface.get(),
            .code = typeface.get(),
        },
        .baseFontSize = 17.0f,
        .viewportHeight = static_cast<float>(height),
        .surfaceWidth = 680.0f,
        .surfaceHeight = static_cast<float>(height),
        .visibleDocumentBottom = layout.totalHeight,
    });
}

void LayoutSensitiveBehavior() {
    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(
        "# Title\n\n"
        "| A | B |\n"
        "| - | - |\n"
        "| left | right |\n\n"
        "```cpp\n"
        "#include <vector>\n"
        "*pointer = value;\n"
        ">literal\n"
        "int main() { return 0; }\n"
        "```\n\n"
        "![diagram](diagram.png)\n");

    const auto imageProvider = [](const std::string& url) {
        return url == "diagram.png" ? std::pair<float, float>{640.0f, 320.0f} : std::pair<float, float>{0.0f, 0.0f};
    };

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    SkTypeface* typefacePtr = typeface.get();
    const auto normal = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, typefacePtr, 17.0f, imageProvider);
    const auto zoomed = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, typefacePtr, 24.0f, imageProvider);
    const auto narrow = mdviewer::LayoutEngine::ComputeLayout(doc, 480.0f, typefacePtr, 17.0f, imageProvider);

    Require(HasBlockType(normal, mdviewer::BlockType::Table), "layout should contain table block");
    Require(HasBlockType(normal, mdviewer::BlockType::CodeBlock), "layout should contain code block");
    Require(normal.plainText.find("A\tB\nleft\tright") != std::string::npos, "table layout should preserve tabular plain text");
    Require(normal.plainText.find("#include <vector>\n*pointer = value;\n>literal") != std::string::npos,
            "Markdown normalization must not mutate fenced code");
    Require(zoomed.totalHeight > normal.totalHeight, "zoomed layout should increase total height");

    const auto& imageBlock = normal.blocks.back();
    Require(!imageBlock.lines.empty(), "image block should create a line");
    Require(!imageBlock.lines[0].runs.empty(), "image line should contain an image run");
    Require(imageBlock.lines[0].runs[0].kind == mdviewer::InlineKind::Image, "last block should be image run");
    RequireNear(imageBlock.lines[0].runs[0].imageHeight * 2.0f, imageBlock.lines[0].runs[0].imageWidth, 0.5f, "image aspect ratio should be preserved");

    const auto& normalTable = FirstBlockOfType(normal, mdviewer::BlockType::Table);
    const auto& narrowTable = FirstBlockOfType(narrow, mdviewer::BlockType::Table);
    Require(narrowTable.bounds.width() < normalTable.bounds.width(), "table width should relayout with viewport width");

    const auto wideInsets = mdviewer::GetDocumentHorizontalInsets(900.0f);
    const auto compactInsets = mdviewer::GetDocumentHorizontalInsets(480.0f);
    RequireNear(wideInsets.left, 40.0f, 0.001f, "wide documents should retain the established left margin");
    RequireNear(wideInsets.right, 104.0f, 0.001f, "wide documents should retain the established right margin");
    RequireNear(compactInsets.left, 12.0f, 0.001f, "compact documents should reduce their left margin");
    RequireNear(compactInsets.right, 12.0f, 0.001f, "compact documents should reduce their right margin");
    RequireNear(narrow.blocks.front().bounds.left(), compactInsets.left, 0.001f, "compact layout should apply its responsive left margin");
    RequireNear(
        480.0f - narrow.blocks.front().bounds.right(),
        compactInsets.right,
        0.001f,
        "compact layout should apply its responsive right margin");

    const auto wideTableLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "| Engine | Checkpoint / KV | Prefill tok/s | TTFT | Effective D2H | ITL | Sampled peak VRAM | Notes |\n"
            "| - | - | - | - | - | - | - | - |\n"
            "| gem16 | direct-FP8-NVFP4-checkpoint | 5866.86 | 2792.64 ms | 87.66 | 11.408 ms | 11746 MiB | reproducible-result |\n"),
        420.0f,
        typefacePtr,
        17.0f);
    const auto& wideTable = FirstBlockOfType(wideTableLayout, mdviewer::BlockType::Table);
    Require(wideTable.horizontalContentWidth > wideTable.horizontalViewportWidth,
            "wide tables should retain natural columns and expose horizontal overflow");
    Require(!wideTable.children.empty() && wideTable.children[0].bounds.width() > wideTable.bounds.width(),
            "table rows should keep their content width behind the clipped viewport");

    mdviewer::AppState renderedTableState;
    renderedTableState.docLayout = wideTableLayout;
    renderedTableState.outlineCollapsed = true;
    const int tableRenderHeight = static_cast<int>(std::ceil(wideTableLayout.totalHeight + 20.0f));
    const sk_sp<SkSurface> tableSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(420, tableRenderHeight));
    Require(tableSurface != nullptr, "table scrollbar regression should create a raster surface");
    std::vector<mdviewer::HorizontalScrollbarRegion> tableScrollbars;
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = tableSurface->getCanvas(),
        .appState = &renderedTableState,
        .palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark),
        .typefaces = mdviewer::DocumentTypefaceSet{
            .fontMgr = fontMgr.get(),
            .regular = typefacePtr,
            .bold = typefacePtr,
            .heading = typefacePtr,
            .code = typefacePtr,
        },
        .baseFontSize = 17.0f,
        .viewportHeight = static_cast<float>(tableRenderHeight),
        .surfaceWidth = 420.0f,
        .surfaceHeight = static_cast<float>(tableRenderHeight),
        .visibleDocumentBottom = wideTableLayout.totalHeight,
        .addHorizontalScrollbar = [&](const auto& region) { tableScrollbars.push_back(region); },
    });
    RequireEqual(tableScrollbars.size(), static_cast<size_t>(1), "one wide table should expose one horizontal scrollbar");

    const auto longTokenLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "https://example.com/this-is-one-deliberately-unbroken-token-that-must-not-escape-the-document-viewport\n"),
        280.0f,
        typefacePtr,
        17.0f);
    Require(!longTokenLayout.blocks.empty() && longTokenLayout.blocks[0].lines.size() > 1,
            "very long unbroken tokens should wrap at UTF-8 boundaries");

    const auto overflowingCodeLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "```cpp\n"
            "const std::string message = \"This deliberately long C++ source line must remain on one visual code line\";\n"
            "```\n"),
        320.0f,
        typefacePtr,
        17.0f);
    const auto& overflowingCode = FirstBlockOfType(overflowingCodeLayout, mdviewer::BlockType::CodeBlock);
    RequireEqual(overflowingCode.lines.size(), static_cast<size_t>(1), "overflowing code should preserve source lines instead of wrapping");
    Require(
        overflowingCode.codeContentWidth > overflowingCode.codeViewportWidth,
        "overflowing code should record a horizontal scroll range");

    mdviewer::AppState codeScrollState;
    codeScrollState.horizontalScrollbars.push_back(mdviewer::HorizontalScrollbarRegion{
        .viewportRect = SkRect::MakeXYWH(10.0f, 20.0f, 200.0f, 100.0f),
        .trackRect = SkRect::MakeXYWH(10.0f, 110.0f, 200.0f, 6.0f),
        .thumbRect = SkRect::MakeXYWH(10.0f, 110.0f, 50.0f, 6.0f),
        .blockTextStart = 42,
        .maxScroll = 600.0f,
    });
    Require(
        mdviewer::BeginHorizontalScrollbarInteraction(codeScrollState, 20.0f, 112.0f),
        "code scrollbar thumb should begin a drag");
    Require(mdviewer::UpdateHorizontalScrollbarDrag(codeScrollState, 160.0f), "dragging a code scrollbar should change its offset");
    Require(codeScrollState.horizontalScrollOffsets[42] > 0.0f, "code scrollbar drag should move the selected block horizontally");
    mdviewer::EndHorizontalScrollbarDrag(codeScrollState);
    Require(!codeScrollState.isDraggingHorizontalScrollbar, "code scrollbar drag should end cleanly");
    const float draggedOffset = codeScrollState.horizontalScrollOffsets[42];
    Require(
        mdviewer::ScrollHorizontalBlockAtPoint(codeScrollState, 50.0f, 50.0f, 40.0f),
        "horizontal wheel input over a code block should be consumed");
    Require(codeScrollState.horizontalScrollOffsets[42] > draggedOffset, "horizontal wheel input should advance that code block only");

    mdviewer::AppState renderedCodeState;
    renderedCodeState.sourceText = "long code";
    renderedCodeState.docLayout = overflowingCodeLayout;
    renderedCodeState.outlineCollapsed = true;
    const int renderHeight = static_cast<int>(std::ceil(overflowingCodeLayout.totalHeight + 40.0f));
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, renderHeight));
    Require(surface != nullptr, "code scrollbar regression should create a raster surface");
    std::vector<mdviewer::HorizontalScrollbarRegion> renderedScrollbars;
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = surface->getCanvas(),
        .appState = &renderedCodeState,
        .palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark),
        .typefaces = mdviewer::DocumentTypefaceSet{
            .fontMgr = fontMgr.get(),
            .regular = typefacePtr,
            .bold = typefacePtr,
            .heading = typefacePtr,
            .code = typefacePtr,
        },
        .baseFontSize = 17.0f,
        .viewportHeight = static_cast<float>(renderHeight),
        .surfaceWidth = 320.0f,
        .surfaceHeight = static_cast<float>(renderHeight),
        .visibleDocumentBottom = overflowingCodeLayout.totalHeight,
        .addHorizontalScrollbar = [&](const auto& region) { renderedScrollbars.push_back(region); },
    });
    RequireEqual(renderedScrollbars.size(), static_cast<size_t>(1), "renderer should expose one scrollbar for one overflowing code block");
    Require(
        renderedScrollbars[0].thumbRect.width() < renderedScrollbars[0].trackRect.width(),
        "overflowing code scrollbar should have movable thumb geometry");
}

void PdfExportWritesFile() {
    TempDir temp;
    const fs::path sourcePath = temp.Path() / "export-source.md";
    const fs::path outputPath = temp.Path() / "export-output.pdf";
    const std::string source =
        "# Export Title\n\n"
        "This paragraph should be rendered into a PDF file.\n\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n";
    WriteText(sourcePath, source);

    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(source);
    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    mdviewer::DocumentTypefaceSet typefaces;
    typefaces.fontMgr = fontMgr.get();
    typefaces.regular = typeface.get();
    typefaces.bold = typeface.get();
    typefaces.heading = typeface.get();
    typefaces.code = typeface.get();

    mdviewer::PdfExportRequest request;
    request.outputPath = outputPath;
    request.sourcePath = sourcePath;
    request.sourceText = source;
    request.document = doc;
    request.theme = mdviewer::ThemeMode::Light;
    request.baseFontSize = mdviewer::kDefaultBaseFontSize;
    request.typefaces = typefaces;
    request.layoutTypeface = typeface.get();

    const mdviewer::PdfExportStatus status = mdviewer::ExportMarkdownToPdf(request);
#if MDVIEWER_ENABLE_PDF
    Require(
        status == mdviewer::PdfExportStatus::Success,
        std::string("PDF export should succeed: ") + mdviewer::PdfExportStatusMessage(status));
    Require(fs::exists(outputPath), "PDF output file should exist");
    Require(fs::file_size(outputPath) > 500, "PDF output file should contain rendered data");

    std::ifstream input(outputPath, std::ios::binary);
    std::string header(5, '\0');
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    RequireEqual(header, std::string("%PDF-"), "PDF output should have a PDF header");
#else
    Require(
        status == mdviewer::PdfExportStatus::PdfBackendUnavailable,
        "disabled PDF builds should report an unavailable backend");
    Require(!fs::exists(outputPath), "disabled PDF builds should not create an output file");
#endif
}

void MenuLayoutHitTesting() {
    const std::vector<float> itemWidths = {30.0f, 40.0f};
    const auto layout = mdviewer::ComputeMenuBarLayout(500.0f, 42.0f, itemWidths);
    RequireEqual(layout.itemRects.size(), static_cast<size_t>(2), "menu item rects should match item widths");

    auto hit = mdviewer::HitTestMenuBarLayout(layout, layout.itemRects[0].centerX(), layout.itemRects[0].centerY());
    Require(hit.target == mdviewer::MenuBarHitTarget::MenuItem, "first menu item should hit as menu item");
    RequireEqual(hit.menuIndex, 0, "first menu item should report index 0");
    RequireEqual(mdviewer::MenuBarStateIndexFromHit(hit), 0, "menu item hit should map to hover index");

    hit = mdviewer::HitTestMenuBarLayout(layout, layout.backRect.centerX(), layout.backRect.centerY());
    Require(hit.target == mdviewer::MenuBarHitTarget::GoBack, "back toolbar button should be typed");
    RequireEqual(mdviewer::MenuBarStateIndexFromHit(hit), -2, "back hit should map to render hover id");

    hit = mdviewer::HitTestMenuBarLayout(layout, layout.forwardRect.centerX(), layout.forwardRect.centerY());
    Require(hit.target == mdviewer::MenuBarHitTarget::GoForward, "forward toolbar button should be typed");

    hit = mdviewer::HitTestMenuBarLayout(layout, layout.zoomOutRect.centerX(), layout.zoomOutRect.centerY());
    Require(hit.target == mdviewer::MenuBarHitTarget::ZoomOut, "zoom out toolbar button should be typed");

    hit = mdviewer::HitTestMenuBarLayout(layout, layout.zoomInRect.centerX(), layout.zoomInRect.centerY());
    Require(hit.target == mdviewer::MenuBarHitTarget::ZoomIn, "zoom in toolbar button should be typed");

    hit = mdviewer::HitTestMenuBarLayout(layout, 250.0f, 100.0f);
    Require(!hit.HasHit(), "point outside menu bar should not hit");
    RequireEqual(mdviewer::MenuBarStateIndexFromHit(hit), -1, "miss should map to no hover");

    const std::vector<mdviewer::DropdownItem> items = {
        {"Open", false},
        {"", true},
        {"Exit", false},
    };
    const SkRect dropdown = mdviewer::ComputeDropdownLayout(12.0f, 42.0f, items, nullptr);
    RequireNear(dropdown.left(), 12.0f, 0.001f, "dropdown x should be preserved");
    RequireNear(dropdown.top(), 42.0f, 0.001f, "dropdown y should be preserved");
    RequireNear(dropdown.height(), 90.0f, 0.001f, "dropdown height should be item count times item height");
    RequireEqual(mdviewer::HitTestDropdownLayout(dropdown, 30.0f, 20.0f, 45.0f), 0, "first dropdown row should hit");
    RequireEqual(mdviewer::HitTestDropdownLayout(dropdown, 30.0f, 20.0f, 75.0f), 1, "separator row should still report its index");
    RequireEqual(mdviewer::HitTestDropdownLayout(dropdown, 30.0f, 20.0f, 140.0f), -1, "point outside dropdown should miss");
}

void SyntaxHighlightingCacheAndFallback() {
    using mdviewer::syntax::HighlightStatus;

    const auto findRoleForText = [](const std::vector<mdviewer::InlineRun>& runs, const std::string& text) {
        const auto found = std::find_if(runs.begin(), runs.end(), [&](const auto& run) {
            return run.text.find(text) != std::string::npos;
        });
        return found == runs.end() ? mdviewer::SyntaxRole::None : found->syntaxRole;
    };

    mdviewer::syntax::ClearHighlightCache();
    const std::vector<mdviewer::InlineRun> codeRuns = {
        mdviewer::InlineRun{.text = "class Widget { public: Widget(); };\n"},
    };
    mdviewer::syntax::HighlightOptions generousOptions;
    generousOptions.timeBudget = std::chrono::seconds(5);

    const auto first = mdviewer::syntax::HighlightCodeBlock("cpp", codeRuns, generousOptions);
    Require(
        first.status == HighlightStatus::Highlighted,
        "known C++ code should be highlighted (status " +
            std::to_string(static_cast<int>(first.status)) + ")");
    RequireEqual(
        MergeInlineRunText(first.runs),
        MergeInlineRunText(codeRuns),
        "syntax highlighting should preserve the exact source text");
    Require(
        std::any_of(first.runs.begin(), first.runs.end(), [](const auto& run) {
            return run.syntaxRole != mdviewer::SyntaxRole::None;
        }),
        "syntax highlighting should assign at least one independent syntax role");
    Require(
        std::all_of(first.runs.begin(), first.runs.end(), [](const auto& run) {
            return run.formatting == mdviewer::InlineFormatting::None &&
                   run.kind == mdviewer::InlineKind::Text;
        }),
        "syntax roles should not replace formatting or content-kind metadata");

    const std::vector<mdviewer::InlineRun> ordinaryCppRuns = {
        mdviewer::InlineRun{
            .text =
                "#include <vector>\n\n"
                "void update(int* pointer) {\n"
                "    *pointer = 42;\n"
                "    // comment\n"
                "}\n"},
    };
    const auto ordinaryCpp = mdviewer::syntax::HighlightCodeBlock("cpp", ordinaryCppRuns, generousOptions);
    Require(ordinaryCpp.status == HighlightStatus::Highlighted, "ordinary C++ code should inherit the base C highlights");
    Require(findRoleForText(ordinaryCpp.runs, "#include") == mdviewer::SyntaxRole::Keyword, "C++ preprocessor directives should be highlighted");
    Require(findRoleForText(ordinaryCpp.runs, "<vector>") == mdviewer::SyntaxRole::String, "C++ system include paths should be highlighted");
    Require(findRoleForText(ordinaryCpp.runs, "void") == mdviewer::SyntaxRole::Type, "C++ primitive types should be highlighted");
    Require(findRoleForText(ordinaryCpp.runs, "update") == mdviewer::SyntaxRole::Function, "C++ function declarations should be highlighted");
    Require(findRoleForText(ordinaryCpp.runs, "// comment") == mdviewer::SyntaxRole::Comment, "C++ comments should be highlighted");

    const auto second = mdviewer::syntax::HighlightCodeBlock("c++", codeRuns, generousOptions);
    Require(second.status == HighlightStatus::Highlighted, "language aliases should reuse highlighted output");
    const auto cacheStats = mdviewer::syntax::GetHighlightCacheStats();
    RequireEqual(cacheStats.misses, static_cast<size_t>(2), "the two distinct C++ snippets should miss the cache once each");
    RequireEqual(cacheStats.hits, static_cast<size_t>(1), "equivalent language alias should hit the cache");
    RequireEqual(cacheStats.entries, static_cast<size_t>(2), "cache should store both canonical C++ results");

    mdviewer::syntax::HighlightOptions immediateTimeout;
    immediateTimeout.timeBudget = std::chrono::milliseconds(0);
    immediateTimeout.useCache = false;
    const auto timedOut = mdviewer::syntax::HighlightCodeBlock("cpp", codeRuns, immediateTimeout);
    Require(timedOut.status == HighlightStatus::TimedOut, "expired highlight work should report a timeout");
    RequireEqual(
        MergeInlineRunText(timedOut.runs),
        MergeInlineRunText(codeRuns),
        "timed-out highlighting should fall back to the original plain code");

    const auto unsupported = mdviewer::syntax::HighlightCodeBlock("not-a-language", codeRuns, generousOptions);
    Require(
        unsupported.status == HighlightStatus::UnsupportedLanguage,
        "unknown language tags should remain an expected plain-text fallback");
}

void ScrollAnchorPreservesReadingPosition() {
    std::ostringstream source;
    for (int index = 0; index < 30; ++index) {
        source << "## Section " << index << "\n\n";
        source << "This paragraph gives the layout enough text to wrap and move when the font size changes. ";
        source << "The anchor should keep this area visible after relayout.\n\n";
    }

    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(source.str());
    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    SkTypeface* typefacePtr = typeface.get();

    const auto normal = mdviewer::LayoutEngine::ComputeLayout(doc, 760.0f, typefacePtr, 17.0f);
    const auto zoomed = mdviewer::LayoutEngine::ComputeLayout(doc, 760.0f, typefacePtr, 24.0f);
    const auto wide = mdviewer::LayoutEngine::ComputeLayout(doc, 920.0f, typefacePtr, 17.0f);
    const auto narrow = mdviewer::LayoutEngine::ComputeLayout(doc, 420.0f, typefacePtr, 17.0f);
    Require(normal.blocks.size() > 20, "fixture should produce enough blocks");

    const float viewportHeight = 420.0f;
    const float oldScrollOffset = normal.blocks[18].bounds.top();
    const mdviewer::ScrollAnchor anchor = mdviewer::CaptureScrollAnchor(normal, oldScrollOffset, viewportHeight);
    Require(anchor.valid, "scroll anchor should capture a visible line");

    mdviewer::AppState appState;
    appState.docLayout = zoomed;
    mdviewer::RestoreScrollAnchor(
        appState,
        anchor,
        viewportHeight,
        std::max(0.0f, zoomed.totalHeight - viewportHeight));

    const mdviewer::ScrollAnchor restoredAnchor = mdviewer::CaptureScrollAnchor(zoomed, appState.scrollOffset, viewportHeight);
    Require(restoredAnchor.valid, "restored scroll offset should capture a visible line");
    Require(restoredAnchor.textPosition <= anchor.textPosition, "relayout should keep the original text at or after the top visible line");
    Require(anchor.textPosition - restoredAnchor.textPosition < 160, "relayout should keep the original text near the viewport top");

    mdviewer::AppState repeatedResizeState;
    repeatedResizeState.docLayout = normal;
    repeatedResizeState.scrollOffset = oldScrollOffset;
    const mdviewer::ScrollAnchor stableAnchor = mdviewer::GetRelayoutScrollAnchor(repeatedResizeState, viewportHeight);
    Require(stableAnchor.valid, "stable resize anchor should capture a visible line");

    for (int cycle = 0; cycle < 8; ++cycle) {
        const mdviewer::ScrollAnchor relayoutAnchor = mdviewer::GetRelayoutScrollAnchor(repeatedResizeState, viewportHeight);
        repeatedResizeState.docLayout = (cycle % 2 == 0) ? narrow : wide;
        mdviewer::RestoreScrollAnchor(
            repeatedResizeState,
            relayoutAnchor,
            viewportHeight,
            std::max(0.0f, repeatedResizeState.docLayout.totalHeight - viewportHeight));
        mdviewer::RememberRelayoutScrollAnchor(repeatedResizeState, relayoutAnchor);
    }

    Require(repeatedResizeState.relayoutScrollAnchor.has_value(), "repeated resize should keep a stable relayout anchor");
    RequireEqual(
        repeatedResizeState.relayoutScrollAnchor->textPosition,
        stableAnchor.textPosition,
        "repeated resize should not recapture an earlier wrapped line start");

    mdviewer::ApplyWheelScroll(
        repeatedResizeState,
        -40.0f,
        std::max(0.0f, repeatedResizeState.docLayout.totalHeight - viewportHeight));
    Require(!repeatedResizeState.relayoutScrollAnchor.has_value(), "manual scroll should clear the stable relayout anchor");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"ConfigParsingAndSaving", ConfigParsingAndSaving},
        {"RecentFilesAndHistory", RecentFilesAndHistory},
        {"ConfigPathMigration", ConfigPathMigration},
        {"LinkResolution", LinkResolution},
        {"SvgImageRendering", SvgImageRendering},
        {"HeadingAnchors", HeadingAnchors},
        {"HitTestingMeasuresOnlyClosestLine", HitTestingMeasuresOnlyClosestLine},
        {"Utf8Boundaries", Utf8Boundaries},
        {"MarkdownSafetyLimits", MarkdownSafetyLimits},
        {"DocumentSizeLimit", DocumentSizeLimit},
        {"MarkdownCorrectnessFoundation", MarkdownCorrectnessFoundation},
        {"SafeHtmlSubset", SafeHtmlSubset},
        {"GithubAlerts", GithubAlerts},
        {"LayoutSensitiveBehavior", LayoutSensitiveBehavior},
        {"PdfExportWritesFile", PdfExportWritesFile},
        {"MenuLayoutHitTesting", MenuLayoutHitTesting},
        {"SyntaxHighlightingCacheAndFallback", SyntaxHighlightingCacheAndFallback},
        {"ScrollAnchorPreservesReadingPosition", ScrollAnchorPreservesReadingPosition},
    };

    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }

    return 0;
}
