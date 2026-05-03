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
        "recent_file_0=C:/docs/one.md\n"
        "recent_file_1=\n"
        "recent_file_2=C:/docs/two.md\n");

    const auto loaded = mdviewer::LoadAppConfig(configPath);
    Require(loaded.has_value(), "config should load");
    Require(loaded->theme == mdviewer::ThemeMode::Dark, "theme should parse");
    RequireEqual(loaded->fontFamilyUtf8, std::string("Example Font"), "font family should trim");
    RequireNear(loaded->baseFontSize, mdviewer::ClampBaseFontSize(999.0f), 0.001f, "font size should clamp");
    RequireEqual(loaded->recentFilesUtf8.size(), static_cast<size_t>(2), "empty recent entries should be skipped");
    RequireEqual(loaded->recentFilesUtf8[1], std::string("C:/docs/two.md"), "recent files should preserve order");

    WriteText(configPath, "[app]\nbase_font_size=not-a-number\ntheme=missing\n");
    const auto invalid = mdviewer::LoadAppConfig(configPath);
    Require(invalid.has_value(), "invalid values still produce defaults");
    Require(invalid->theme == mdviewer::ThemeMode::Light, "invalid theme should fall back to light");
    RequireNear(invalid->baseFontSize, mdviewer::kDefaultBaseFontSize, 0.001f, "invalid font size should fall back");

    mdviewer::AppConfig saved;
    saved.theme = mdviewer::ThemeMode::Sepia;
    saved.fontFamilyUtf8 = "Saved Font";
    saved.baseFontSize = 21.0f;
    saved.recentFilesUtf8 = {"C:/docs/a.md", "C:/docs/b.md"};
    Require(mdviewer::SaveAppConfig(configPath, saved), "config should save");
    const auto roundTrip = mdviewer::LoadAppConfig(configPath);
    Require(roundTrip.has_value(), "saved config should reload");
    Require(roundTrip->theme == mdviewer::ThemeMode::Sepia, "saved theme should round-trip");
    RequireEqual(roundTrip->fontFamilyUtf8, saved.fontFamilyUtf8, "saved font should round-trip");
    Require(roundTrip->recentFilesUtf8 == saved.recentFilesUtf8, "saved recent files should round-trip");
}

void RecentFilesAndHistory() {
    TempDir temp;
    const fs::path configPath = temp.Path() / "mdviewer.ini";
    std::ostringstream config;
    config << "[app]\n";
    for (int index = 0; index < 10; ++index) {
        config << "recent_file_" << index << '=' << (temp.Path() / ("file" + std::to_string(index) + ".md")).string() << '\n';
    }
    config << "recent_file_10=" << (temp.Path() / "file5.md").string() << '\n';
    WriteText(configPath, config.str());

    mdviewer::ViewerController controller;
    controller.SetConfigPath(configPath);
    Require(controller.LoadConfig(), "controller config should load");
    const auto& recent = controller.GetRecentFiles();
    RequireEqual(recent.size(), static_cast<size_t>(8), "recent files should be capped");
    Require(recent.front().filename() == "file5.md", "duplicate recent file should move to front");

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
    RequireEqual(mdviewer::MakeHeadingAnchor("日本語"), std::string(), "Unicode-only headings currently have no fallback slug");

    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(
        "# Hello\n"
        "## Hello\n"
        "# 🚀 Launch\n"
        "# 日本語\n");
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, nullptr, mdviewer::kDefaultBaseFontSize);
    Require(layout.anchors.contains("hello"), "first duplicate heading should use base slug");
    Require(layout.anchors.contains("hello-2"), "second duplicate heading should get numeric suffix");
    Require(layout.anchors.contains("launch"), "emoji plus ASCII heading should anchor on ASCII text");
    Require(!layout.anchors.contains(""), "empty Unicode fallback should not be inserted as an anchor");
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

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"ConfigParsingAndSaving", ConfigParsingAndSaving},
        {"RecentFilesAndHistory", RecentFilesAndHistory},
        {"LinkResolution", LinkResolution},
        {"HeadingAnchors", HeadingAnchors},
        {"LayoutSensitiveBehavior", LayoutSensitiveBehavior},
        {"MenuLayoutHitTesting", MenuLayoutHitTesting},
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
