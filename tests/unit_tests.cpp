#include <algorithm>
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
#include "app/heading_anchor.h"
#include "app/link_resolver.h"
#include "app/viewer_controller.h"
#include "layout/layout_engine.h"
#include "markdown/markdown_parser.h"
#include "render/menu_renderer.h"
#include "render/typography.h"
#include "util/skia_font_utils.h"
#include "view/document_interaction.h"
#include "view/document_outline.h"

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
        "recent_file_0=C:/docs/one.md\n"
        "recent_file_0_opened_at=1700000000\n"
        "recent_file_1=\n"
        "recent_file_2=C:/docs/two.md\n");

    const auto loaded = mdviewer::LoadAppConfig(configPath);
    Require(loaded.has_value(), "config should load");
    Require(loaded->theme == mdviewer::ThemeMode::Dark, "theme should parse");
    Require(loaded->outlineSide == mdviewer::OutlineSide::Right, "outline side should parse");
    RequireEqual(loaded->fontFamilyUtf8, std::string("Example Font"), "font family should trim");
    RequireNear(loaded->baseFontSize, mdviewer::ClampBaseFontSize(999.0f), 0.001f, "font size should clamp");
    RequireEqual(loaded->recentFiles.size(), static_cast<size_t>(2), "empty recent entries should be skipped");
    RequireEqual(loaded->recentFiles[0].pathUtf8, std::string("C:/docs/one.md"), "recent files should preserve order");
    RequireEqual(loaded->recentFiles[0].openedAtUnixSeconds, 1700000000LL, "recent opened timestamp should parse");
    RequireEqual(loaded->recentFiles[1].pathUtf8, std::string("C:/docs/two.md"), "recent files should preserve sparse index order");

    WriteText(configPath, "[app]\nbase_font_size=not-a-number\ntheme=missing\n");
    const auto invalid = mdviewer::LoadAppConfig(configPath);
    Require(invalid.has_value(), "invalid values still produce defaults");
    Require(invalid->theme == mdviewer::ThemeMode::Light, "invalid theme should fall back to light");
    RequireNear(invalid->baseFontSize, mdviewer::kDefaultBaseFontSize, 0.001f, "invalid font size should fall back");

    mdviewer::AppConfig saved;
    saved.theme = mdviewer::ThemeMode::Sepia;
    saved.outlineSide = mdviewer::OutlineSide::Right;
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
    WriteText(current, "# Home\n");
    WriteText(sibling, "# Other\n");
    WriteText(child, "# Child\n");
    WriteText(textNoExtension, "license text\n");
    WriteText(binary, std::string("MZ\0\0binary", 10));

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

    target = mdviewer::ResolveLinkTarget(current, sibling.string(), true);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "forceExternal should override internal text handling");
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
}

void LayoutSensitiveBehavior() {
    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(
        "# Title\n\n"
        "| A | B |\n"
        "| - | - |\n"
        "| left | right |\n\n"
        "```cpp\n"
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
    Require(zoomed.totalHeight > normal.totalHeight, "zoomed layout should increase total height");

    const auto& imageBlock = normal.blocks.back();
    Require(!imageBlock.lines.empty(), "image block should create a line");
    Require(!imageBlock.lines[0].runs.empty(), "image line should contain an image run");
    Require(imageBlock.lines[0].runs[0].style == mdviewer::InlineStyle::Image, "last block should be image run");
    RequireNear(imageBlock.lines[0].runs[0].imageHeight * 2.0f, imageBlock.lines[0].runs[0].imageWidth, 0.5f, "image aspect ratio should be preserved");

    const auto& normalTable = FirstBlockOfType(normal, mdviewer::BlockType::Table);
    const auto& narrowTable = FirstBlockOfType(narrow, mdviewer::BlockType::Table);
    Require(narrowTable.bounds.width() < normalTable.bounds.width(), "table width should relayout with viewport width");
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
        {"HeadingAnchors", HeadingAnchors},
        {"LayoutSensitiveBehavior", LayoutSensitiveBehavior},
        {"MenuLayoutHitTesting", MenuLayoutHitTesting},
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
