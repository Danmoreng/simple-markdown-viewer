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
#include "render/print_document.h"
#include "render/syntax/tree_sitter_highlighter.h"
#include "render/typography.h"
#include "text/complex_text_runtime.h"
#include "text/shaped_text_layout.h"
#include "text/text_geometry.h"
#include "util/skia_font_utils.h"
#include "util/utf8.h"
#include "view/document_hit_test.h"
#include "view/document_context_menu.h"
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

mdviewer::DocumentTypefaceSet MakeTestTypefaces(
    SkFontMgr* fontManager,
    SkTypeface* regular,
    SkTypeface* bold = nullptr,
    SkTypeface* heading = nullptr,
    SkTypeface* code = nullptr) {
    return mdviewer::DocumentTypefaceSet{
        .fontMgr = fontManager,
        .regular = regular,
        .bold = bold ? bold : regular,
        .heading = heading ? heading : (bold ? bold : regular),
        .code = code ? code : regular,
    };
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

fs::path SourceRoot() {
    const fs::path sourceFile = fs::path(__FILE__);
    if (sourceFile.is_absolute()) {
        return sourceFile.parent_path().parent_path();
    }

    const fs::path fromWorkingDirectory = fs::current_path() / sourceFile;
    if (fs::exists(fromWorkingDirectory)) {
        return fs::weakly_canonical(fromWorkingDirectory).parent_path().parent_path();
    }

    const fs::path fromBuildDirectory = fs::current_path().parent_path() / sourceFile;
    Require(fs::exists(fromBuildDirectory), "could not resolve repository root from " + sourceFile.string());
    return fs::weakly_canonical(fromBuildDirectory).parent_path().parent_path();
}

std::string ReadText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "could not open " + path.string() + " for reading");
    std::ostringstream content;
    content << input.rdbuf();
    Require(input.good() || input.eof(), "could not read " + path.string());
    return content.str();
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

const mdviewer::BlockLayout* FindLayoutBlockOfType(
    const std::vector<mdviewer::BlockLayout>& blocks,
    mdviewer::BlockType type) {
    for (const auto& block : blocks) {
        if (block.type == type) {
            return &block;
        }
        if (const auto* nested = FindLayoutBlockOfType(block.children, type)) {
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
        "window_x=-1200\n"
        "window_y=75\n"
        "window_width=1280\n"
        "window_height=800\n"
        "recent_file_0=C:/docs/one.md\n"
        "recent_file_0_opened_at=1700000000\n"
        "recent_file_0_scroll_offset=345.75\n"
        "recent_file_1=\n"
        "recent_file_2=C:/docs/two.md\n"
        "recent_file_2_scroll_offset=not-a-number\n");

    const auto loaded = mdviewer::LoadAppConfig(configPath);
    Require(loaded.has_value(), "config should load");
    Require(loaded->theme == mdviewer::ThemeMode::Dark, "theme should parse");
    Require(loaded->outlineSide == mdviewer::OutlineSide::Right, "outline side should parse");
    RequireNear(loaded->outlineWidth, mdviewer::kMaxOutlineWidth, 0.001f, "outline width should clamp");
    RequireEqual(loaded->fontFamilyUtf8, std::string("Example Font"), "font family should trim");
    RequireNear(loaded->baseFontSize, mdviewer::ClampBaseFontSize(999.0f), 0.001f, "font size should clamp");
    Require(loaded->windowPlacement.has_value(), "complete valid window placement should parse");
    RequireEqual(loaded->windowPlacement->x, -1200, "window placement should preserve negative monitor coordinates");
    RequireEqual(loaded->windowPlacement->y, 75, "window placement should preserve vertical position");
    RequireEqual(loaded->windowPlacement->width, 1280, "window placement should preserve width");
    RequireEqual(loaded->windowPlacement->height, 800, "window placement should preserve height");
    RequireEqual(loaded->recentFiles.size(), static_cast<size_t>(2), "empty recent entries should be skipped");
    RequireEqual(loaded->recentFiles[0].pathUtf8, std::string("C:/docs/one.md"), "recent files should preserve order");
    RequireEqual(loaded->recentFiles[0].openedAtUnixSeconds, 1700000000LL, "recent opened timestamp should parse");
    RequireNear(loaded->recentFiles[0].scrollOffset, 345.75f, 0.001f, "recent scroll offset should parse");
    RequireEqual(loaded->recentFiles[1].pathUtf8, std::string("C:/docs/two.md"), "recent files should preserve sparse index order");
    RequireNear(loaded->recentFiles[1].scrollOffset, 0.0f, 0.001f, "invalid recent scroll offsets should fall back safely");

    WriteText(configPath,
        "[app]\nbase_font_size=not-a-number\noutline_width=not-a-number\ntheme=missing\n"
        "window_x=10\nwindow_y=20\nwindow_width=invalid\nwindow_height=100\n");
    const auto invalid = mdviewer::LoadAppConfig(configPath);
    Require(invalid.has_value(), "invalid values still produce defaults");
    Require(invalid->theme == mdviewer::ThemeMode::Light, "invalid theme should fall back to light");
    RequireNear(invalid->baseFontSize, mdviewer::kDefaultBaseFontSize, 0.001f, "invalid font size should fall back");
    RequireNear(invalid->outlineWidth, mdviewer::kDefaultOutlineWidth, 0.001f, "invalid outline width should fall back");
    Require(!invalid->windowPlacement.has_value(), "incomplete or invalid window placement should be ignored");

    mdviewer::AppConfig saved;
    saved.theme = mdviewer::ThemeMode::Sepia;
    saved.outlineSide = mdviewer::OutlineSide::Right;
    saved.outlineWidth = 344.0f;
    saved.fontFamilyUtf8 = "Saved Font";
    saved.baseFontSize = 21.0f;
    saved.windowPlacement = mdviewer::WindowPlacement{-640, 48, 1440, 900};
    saved.recentFiles = {
        {"C:/docs/a.md", 1700000100, 512.5f},
        {"C:/docs/b.md", 1700000200, 0.0f},
    };
    Require(mdviewer::SaveAppConfig(configPath, saved), "config should save");
    const auto roundTrip = mdviewer::LoadAppConfig(configPath);
    Require(roundTrip.has_value(), "saved config should reload");
    Require(roundTrip->theme == mdviewer::ThemeMode::Sepia, "saved theme should round-trip");
    Require(roundTrip->outlineSide == mdviewer::OutlineSide::Right, "saved outline side should round-trip");
    RequireNear(roundTrip->outlineWidth, saved.outlineWidth, 0.001f, "saved outline width should round-trip");
    RequireEqual(roundTrip->fontFamilyUtf8, saved.fontFamilyUtf8, "saved font should round-trip");
    Require(roundTrip->windowPlacement.has_value(), "saved window placement should round-trip");
    RequireEqual(roundTrip->windowPlacement->x, saved.windowPlacement->x, "saved window x should round-trip");
    RequireEqual(roundTrip->windowPlacement->y, saved.windowPlacement->y, "saved window y should round-trip");
    RequireEqual(roundTrip->windowPlacement->width, saved.windowPlacement->width, "saved window width should round-trip");
    RequireEqual(roundTrip->windowPlacement->height, saved.windowPlacement->height, "saved window height should round-trip");
    RequireEqual(roundTrip->recentFiles.size(), saved.recentFiles.size(), "saved recent files should round-trip");
    RequireEqual(roundTrip->recentFiles[0].pathUtf8, saved.recentFiles[0].pathUtf8, "saved recent path should round-trip");
    RequireEqual(roundTrip->recentFiles[0].openedAtUnixSeconds, saved.recentFiles[0].openedAtUnixSeconds, "saved recent timestamp should round-trip");
    RequireNear(roundTrip->recentFiles[0].scrollOffset, saved.recentFiles[0].scrollOffset, 0.001f, "saved recent scroll offset should round-trip");
}

void RecentFilesAndHistory() {
    TempDir temp;
    const fs::path configPath = temp.Path() / "mdviewer.ini";
    std::ostringstream config;
    config << "[app]\n";
    for (int index = 0; index < 10; ++index) {
        const fs::path recentPath = temp.Path() / ("file" + std::to_string(index) + ".md");
        WriteText(recentPath, "# File " + std::to_string(index) + "\n");
        config << "recent_file_" << index << '=' << recentPath.string() << '\n';
        config << "recent_file_" << index << "_opened_at=" << (1700000000 + index) << '\n';
        config << "recent_file_" << index << "_scroll_offset=" << (125.0f + index) << '\n';
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
    RequireNear(recent.front().scrollOffset, 125.0f, 0.001f, "loaded recent scroll offset should be preserved");
    Require(std::any_of(recent.begin(), recent.end(), [](const mdviewer::RecentFileEntry& entry) {
        return entry.path.filename() == "file5.md";
    }), "duplicate recent file later in config should not create another entry");

    const fs::path openedPath = temp.Path() / "fresh.md";
    WriteText(openedPath, "# Fresh\n");
    const fs::path rememberedPath = recent.front().path;
    Require(
        controller.OpenFile(rememberedPath, 800.0f, {}, {}, {}) == mdviewer::OpenDocumentStatus::Success,
        "opening a remembered recent file should succeed");
    RequireNear(controller.GetAppState().scrollOffset, 125.0f, 0.001f, "opening a recent file should restore its scroll offset");
    controller.GetMutableAppState().scrollOffset = 432.5f;
    Require(
        controller.OpenFile(openedPath, 800.0f, {}, {}, {}) == mdviewer::OpenDocumentStatus::Success,
        "opening a file should succeed");
    Require(controller.GetRecentFiles().front().path.filename() == "fresh.md", "newly opened file should be first in recent files");
    Require(controller.GetRecentFiles().front().openedAtUnixSeconds > 0, "newly opened file should record an opened timestamp");
    Require(
        controller.OpenFile(rememberedPath, 800.0f, {}, {}, {}) == mdviewer::OpenDocumentStatus::Success,
        "reopening a recent file should succeed");
    RequireNear(controller.GetAppState().scrollOffset, 432.5f, 0.001f, "reopening a recent file should restore its latest scroll offset");
    controller.GetMutableAppState().scrollOffset = 678.25f;
    Require(controller.SaveConfig(), "saving controller config should include the current scroll position");
    const auto savedRecentConfig = mdviewer::LoadAppConfig(configPath);
    Require(savedRecentConfig.has_value() && !savedRecentConfig->recentFiles.empty(), "saved recent config should reload");
    RequireNear(savedRecentConfig->recentFiles.front().scrollOffset, 678.25f, 0.001f, "current file scroll should be captured when config is saved");

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
    const fs::path outside = root / "outside.md";
    WriteText(current, "# Home\n");
    WriteText(sibling, "# Other\n");
    WriteText(child, "# Child\n");
    WriteText(textNoExtension, "license text\n");
    WriteText(binary, std::string("MZ\0\0binary", 10));
    WriteText(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\"></svg>");
    WriteText(tldraw, std::string("PK\x03\x04\0binary", 11));
    WriteText(outside, "# Outside\n");

    auto target = mdviewer::ResolveLinkTarget(current, "#Section%201", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "fragment-only link should stay internal");
    RequireEqual(target.path, current, "fragment-only link should target current file");
    RequireEqual(target.fragment, std::string("Section 1"), "fragment should be percent-decoded");

    target = mdviewer::ResolveLinkTarget(current, "Other%20File.md#A%20B", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "percent-encoded markdown path should resolve internally");
    RequireEqual(target.path.lexically_normal(), sibling.lexically_normal(), "encoded spaces should resolve to sibling path");
    RequireEqual(target.fragment, std::string("A B"), "file fragment should decode");
    Require(!target.RequiresConfirmation(), "ordinary document-relative link should not require confirmation");

    target = mdviewer::ResolveLinkTarget(current, "../outside.md", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "outside markdown file should still be an internal document target");
    Require(!target.RequiresConfirmation(), "document links outside the current folder should open without confirmation");

    target = mdviewer::ResolveLinkTarget(current, sibling.string(), false);
    Require(!target.RequiresConfirmation(), "absolute document links should open without confirmation");

    target = mdviewer::ResolveLinkTarget(current, "nested/page.md?ignored=true", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "relative markdown path with query should resolve");
    RequireEqual(target.path.lexically_normal(), child.lexically_normal(), "relative nested path should resolve");

    target = mdviewer::ResolveLinkTarget(current, "LICENSE", false);
    Require(target.kind == mdviewer::LinkTargetKind::InternalDocument, "extensionless known text file should open internally");

    target = mdviewer::ResolveLinkTarget(current, "missing.md", false);
    Require(target.kind == mdviewer::LinkTargetKind::MissingLocalPath, "missing local file should be reported distinctly");
    RequireEqual(target.path.lexically_normal(), (current.parent_path() / "missing.md").lexically_normal(),
                 "missing local file should preserve the document-relative target path");
    Require(!target.RequiresConfirmation(), "missing target should report its error without a separate safety confirmation");

    const auto missingFileUtf8 = (root / "missing file.md").generic_u8string();
    target = mdviewer::ResolveLinkTarget(
        current,
        "file://" + std::string(missingFileUtf8.begin(), missingFileUtf8.end()),
        false);
    Require(target.kind == mdviewer::LinkTargetKind::MissingLocalPath, "missing file URL should be reported distinctly");

    target = mdviewer::ResolveLinkTarget(current, "https://example.com/a%20b#frag", false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalUrl, "https link should open externally");
    RequireEqual(target.externalUrl, std::string("https://example.com/a%20b#frag"), "external URL should be preserved");

    target = mdviewer::ResolveLinkTarget(current, "javascript:alert(1)", false);
    Require(target.kind == mdviewer::LinkTargetKind::Invalid, "unsafe unsupported scheme should be invalid");

    target = mdviewer::ResolveLinkTarget(current, "custom-app:open", false);
    Require(target.kind == mdviewer::LinkTargetKind::Invalid, "custom URL scheme should remain invalid rather than looking like a missing file");

    target = mdviewer::ResolveLinkTarget(current, binary.filename().string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "existing binary path should open as external path");
    Require(target.executableLocalFile, "executable extension should require confirmation");

    target = mdviewer::ResolveLinkTarget(current, svg.string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "SVG links should not be loaded as text documents");
    Require(mdviewer::LoadDocumentFromPath(svg).status == mdviewer::DocumentLoadStatus::BinaryFile,
            "opening an SVG directly should not parse it as a text document");

    target = mdviewer::ResolveLinkTarget(current, tldraw.string(), false);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "tldraw archives should open externally");

    target = mdviewer::ResolveLinkTarget(current, sibling.string(), true);
    Require(target.kind == mdviewer::LinkTargetKind::ExternalPath, "forceExternal should override internal text handling");

    mdviewer::AppState contextState;
    contextState.currentFilePath = current;
    const mdviewer::InteractionTextHit localLinkHit{0, true, "Other%20File.md"};
    const auto contextMenu = mdviewer::BuildDocumentContextMenu(contextState, localLinkHit);
    const auto hasContextCommand = [&](mdviewer::DocumentContextCommand command) {
        return std::any_of(contextMenu.items.begin(), contextMenu.items.end(), [&](const auto& item) {
            return item.command == command;
        });
    };
    Require(hasContextCommand(mdviewer::DocumentContextCommand::RevealLinkTarget), "local link context menu should offer reveal target");
    Require(hasContextCommand(mdviewer::DocumentContextCommand::ReloadDocument), "document context menu should offer reload");
    Require(hasContextCommand(mdviewer::DocumentContextCommand::CopyDocumentPath), "document context menu should offer copy path");
    Require(!hasContextCommand(mdviewer::DocumentContextCommand::RevealDocument), "link context menu should not duplicate the document file-manager action");
    RequireEqual(contextMenu.localLinkPath.lexically_normal(), sibling.lexically_normal(), "context menu should retain resolved local link path");

    const mdviewer::InteractionTextHit imageHit{
        .position = 0,
        .valid = true,
        .url = "diagram.svg",
        .kind = mdviewer::InlineKind::Image,
        .linkTarget = "diagram.svg",
        .imageSource = "diagram.svg",
    };
    const auto imageContextMenu = mdviewer::BuildDocumentContextMenu(contextState, imageHit);
    const auto hasImageCommand = [&](mdviewer::DocumentContextCommand command) {
        return std::any_of(imageContextMenu.items.begin(), imageContextMenu.items.end(), [&](const auto& item) {
            return item.command == command;
        });
    };
    Require(hasImageCommand(mdviewer::DocumentContextCommand::OpenImage), "image context menu should offer open image");
    Require(hasImageCommand(mdviewer::DocumentContextCommand::CopyImagePath), "image context menu should offer copy image path");
    Require(hasImageCommand(mdviewer::DocumentContextCommand::RevealImage), "local image context menu should offer the file-manager action");
    Require(!hasImageCommand(mdviewer::DocumentContextCommand::RevealLinkTarget),
            "an image linked to itself should not duplicate the file-manager action");
    RequireEqual(imageContextMenu.localImagePath.lexically_normal(), svg.lexically_normal(),
                 "image context menu should retain the resolved local image path");

    const mdviewer::InteractionTextHit tableHit{
        .position = 0,
        .valid = true,
        .tableTsv = "Name\tValue\r\nAlpha\t1",
        .tableCsv = "Name,Value\r\nAlpha,1",
    };
    const auto tableContextMenu = mdviewer::BuildDocumentContextMenu(contextState, tableHit);
    Require(std::any_of(tableContextMenu.items.begin(), tableContextMenu.items.end(), [](const auto& item) {
        return item.command == mdviewer::DocumentContextCommand::CopyTableTsv;
    }), "table context menu should offer TSV copy");
    Require(std::any_of(tableContextMenu.items.begin(), tableContextMenu.items.end(), [](const auto& item) {
        return item.command == mdviewer::DocumentContextCommand::CopyTableCsv;
    }), "table context menu should offer CSV copy");

    const mdviewer::InteractionTextHit backgroundHit{};
    const auto backgroundContextMenu = mdviewer::BuildDocumentContextMenu(contextState, backgroundHit);
    Require(
        std::any_of(backgroundContextMenu.items.begin(), backgroundContextMenu.items.end(), [](const auto& item) {
            return item.command == mdviewer::DocumentContextCommand::RevealDocument;
        }),
        "background context menu should offer the document file-manager action");

    const mdviewer::InteractionTextHit webLinkHit{0, true, "https://example.com"};
    const auto webContextMenu = mdviewer::BuildDocumentContextMenu(contextState, webLinkHit);
    Require(
        std::none_of(webContextMenu.items.begin(), webContextMenu.items.end(), [](const auto& item) {
            return item.command == mdviewer::DocumentContextCommand::RevealLinkTarget;
        }),
        "web link context menu should not offer a local reveal action");

    const mdviewer::InteractionTextHit missingLinkHit{0, true, "missing.md"};
    const auto missingContextMenu = mdviewer::BuildDocumentContextMenu(contextState, missingLinkHit);
    Require(
        std::none_of(missingContextMenu.items.begin(), missingContextMenu.items.end(), [](const auto& item) {
            return item.command == mdviewer::DocumentContextCommand::RevealLinkTarget;
        }),
        "missing link context menu should not offer a reveal action");
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

    const fs::path viewBoxOnlySvg = temp.Path() / "viewbox-only.svg";
    WriteText(
        viewBoxOnlySvg,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 256 128\">"
        "<rect width=\"256\" height=\"128\" fill=\"#33404a\"/></svg>");
    const auto [viewBoxWidth, viewBoxHeight] = cache.GetImageSize(viewBoxOnlySvg.filename().string(), temp.Path());
    RequireNear(viewBoxWidth, 256.0f, 0.01f, "viewBox-only SVG should use its viewBox width as intrinsic width");
    RequireNear(viewBoxHeight, 128.0f, 0.01f, "viewBox-only SVG should use its viewBox height as intrinsic height");
    Require(cache.GetImage(viewBoxOnlySvg.filename().string(), temp.Path(), 256.0f, 128.0f) != nullptr,
            "viewBox-only SVG should rasterize through Skia");

    cache.BeginLiveResize();
    const auto liveResizeImage = cache.GetImage(svg.filename().string(), temp.Path(), 200.0f, 100.0f);
    Require(liveResizeImage.get() == image.get(),
            "live resize should reuse the previously rendered image instead of creating a scaled variant");
    RequireEqual(liveResizeImage->width(), 320, "live resize should preserve the cached image width");
    RequireEqual(liveResizeImage->height(), 160, "live resize should preserve the cached image height");
    cache.EndLiveResize();

    const auto resizedImage = cache.GetImage(svg.filename().string(), temp.Path(), 200.0f, 100.0f);
    Require(resizedImage != nullptr, "image should render at its final size after live resize");
    RequireEqual(resizedImage->width(), 200, "final image should use the requested width after live resize");
    RequireEqual(resizedImage->height(), 100, "final image should use the requested height after live resize");

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
    RequireEqual(mdviewer::MakeHeadingAnchor("RÉSUMÉ ÜBER"), std::string("résumé-über"), "uppercase Latin Unicode letters should be lowercased");
    RequireEqual(mdviewer::MakeHeadingAnchor("ПРИВЕТ Мир"), std::string("привет-мир"), "uppercase Cyrillic letters should be lowercased");
    RequireEqual(mdviewer::MakeHeadingAnchor("ΘΕΜΑ"), std::string("θεμα"), "uppercase Greek letters should be lowercased");
    RequireEqual(mdviewer::MakeHeadingAnchor("C++ & C#"), std::string("c-c"), "ASCII punctuation should be stripped");
    RequireEqual(mdviewer::MakeHeadingAnchor("Emoji ✨ Heading"), std::string("emoji-heading"), "emoji symbols should be stripped without extra hyphens");

    const mdviewer::DocumentModel doc = mdviewer::MarkdownParser::Parse(
        "# Hello\n"
        "## Hello\n"
        "### Hello\n"
        "# Hello 1\n"
        "# Hello-1\n"
        "# 🚀 Launch\n"
        "# 日本語\n"
        "# RÉSUMÉ GUIDE\n"
        "# ПРИВЕТ Мир\n");
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, {}, mdviewer::kDefaultBaseFontSize);
    Require(layout.anchors.contains("hello"), "first duplicate heading should use base slug");
    Require(layout.anchors.contains("hello-1"), "second duplicate heading should start numeric suffixes at one");
    Require(layout.anchors.contains("hello-2"), "third duplicate heading should increment the numeric suffix");
    Require(layout.anchors.contains("hello-1-1"), "a generated suffix should not collide with heading text");
    Require(layout.anchors.contains("hello-1-2"), "later heading text should skip all existing suffix collisions");
    Require(layout.anchors.contains("launch"), "emoji plus ASCII heading should anchor on ASCII text");
    Require(layout.anchors.contains("日本語"), "Unicode-only heading should produce an anchor");
    Require(layout.anchors.contains("résumé-guide"), "Latin Unicode heading should produce an anchor");
    Require(layout.anchors.contains("привет-мир"), "Cyrillic heading should produce a lowercase anchor");
    RequireEqual(layout.outline.size(), static_cast<size_t>(9), "all headings should appear in the document outline");
    RequireEqual(layout.outline[0].level, 1, "outline should preserve heading level");
    RequireEqual(layout.outline[1].slug, std::string("hello-1"), "outline should preserve unique duplicate slug");
    RequireEqual(layout.outline[6].slug, std::string("日本語"), "outline should include Unicode-only heading anchors");

    mdviewer::AppState appState;
    appState.docLayout = layout;
    RequireNear(mdviewer::GetOutlineSidebarWidth(appState), mdviewer::kOutlineSidebarWidth, 0.001f, "headings should enable outline sidebar");
    RequireNear(mdviewer::GetOutlineToggleRect(appState, 900.0f, 30.0f).centerX(), mdviewer::kOutlineSidebarWidth, 0.001f,
                "the left outline toggle should be centered over the document divider");
    Require(mdviewer::HitTestOutlineToggle(appState, mdviewer::kOutlineSidebarWidth, 42.0f, 900.0f, 30.0f),
            "outline toggle should hit on the divider");
    Require(!mdviewer::HitTestOutlineResizeHandle(appState, mdviewer::kOutlineSidebarWidth, 42.0f, 900.0f, 600.0f, 30.0f),
            "the divider resize handle should yield to the overlapping toggle");
    const auto firstOutlineHit = mdviewer::HitTestOutlineSidebar(appState, 24.0f, 54.0f, 900.0f, 30.0f);
    Require(firstOutlineHit.has_value() && *firstOutlineHit == 0, "outline hit test should identify the first row");
    const auto outsideOutlineHit = mdviewer::HitTestOutlineSidebar(appState, mdviewer::kOutlineSidebarWidth + 1.0f, 54.0f, 900.0f, 30.0f);
    Require(!outsideOutlineHit.has_value(), "outline hit test should ignore points outside the sidebar");
    Require(mdviewer::FocusOutlineItem(appState, 1, 10000.0f), "outline item should be focusable");
    RequireEqual(appState.focusedOutlineIndex, static_cast<size_t>(1), "focused outline index should update");
    Require(mdviewer::MoveOutlineFocus(appState, 1, 10000.0f), "outline focus should move down");
    RequireEqual(appState.focusedOutlineIndex, static_cast<size_t>(2), "outline focus should move to the next item");
    appState.outlineCollapsed = true;
    RequireNear(mdviewer::GetOutlineSidebarWidth(appState), mdviewer::kOutlineToggleSize * 0.5f, 0.001f,
                "collapsed outline should reserve only half the overlaid toggle width");
    Require(mdviewer::HitTestOutlineToggle(appState, mdviewer::kOutlineCollapsedWidth, 42.0f, 900.0f, 30.0f),
            "collapsed outline toggle should remain fully clickable on the document edge");
    Require(!mdviewer::HitTestOutlineSidebar(appState, 24.0f, 74.0f, 900.0f, 30.0f).has_value(), "collapsed outline should not hit rows");
    appState.outlineCollapsed = false;
    appState.outlineSide = mdviewer::OutlineSide::Right;
    RequireNear(mdviewer::GetOutlineX(appState, 900.0f), 900.0f - mdviewer::kOutlineSidebarWidth, 0.001f, "right outline should be placed at the right edge");
    Require(mdviewer::HitTestOutlineToggle(appState, 900.0f - mdviewer::kOutlineSidebarWidth, 42.0f, 900.0f, 30.0f),
            "right outline toggle should hit while centered on the inner divider");
    Require(mdviewer::HitTestOutlineSidebar(appState, 900.0f - mdviewer::kOutlineSidebarWidth + 40.0f, 54.0f, 900.0f, 30.0f).has_value(),
            "right outline rows should start at the top beside the toggle");

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
    const auto visibleOutlineHit = mdviewer::HitTestOutlineSidebar(longOutlineState, 24.0f, 54.0f, 900.0f, 30.0f);
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

void ComplexTextOutlineRendering() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "complex-text outline regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());

    const std::string source =
        "# English heading\n\n"
        "## مقدمة\n\n"
        "## مقدمة\n\n"
        "## כותרת בעברית\n";
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(source);
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        document,
        380.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    RequireEqual(layout.outline.size(), static_cast<size_t>(4),
                 "Arabic and Hebrew headings should remain in the outline");
    for (const size_t index : {static_cast<size_t>(1), static_cast<size_t>(2), static_cast<size_t>(3)}) {
        const auto& item = layout.outline[index];
        Require(item.hasShapedLine &&
                    item.shapedLine.direction == mdviewer::ResolvedTextDirection::RightToLeft,
                "RTL outline labels should cache a shaped right-to-left visual line");
        Require(!item.shapedLine.runs.empty() &&
                    std::all_of(item.shapedLine.runs.begin(), item.shapedLine.runs.end(), [](const auto& run) {
                        return run.shaped && !run.glyphs.empty() &&
                            run.glyphs.size() == run.glyphPositions.size();
                    }),
                "RTL outline labels should retain HarfBuzz glyph geometry");
    }

    mdviewer::AppState state;
    state.sourceText = source;
    state.docLayout = layout;
    state.outlineSide = mdviewer::OutlineSide::Right;
    state.outlineWidth = 260.0f;
    state.outlineCollapsed = false;
    const mdviewer::ThemePalette palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark);
    constexpr int surfaceWidth = 640;
    constexpr int surfaceHeight = 240;
    constexpr float contentTopInset = 30.0f;
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(surfaceWidth, surfaceHeight));
    Require(surface != nullptr, "complex-text outline regression should create a raster surface");
    surface->getCanvas()->clear(palette.windowBackground);
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = surface->getCanvas(),
        .appState = &state,
        .palette = palette,
        .typefaces = typefaces,
        .baseFontSize = mdviewer::kDefaultBaseFontSize,
        .contentTopInset = contentTopInset,
        .viewportHeight = surfaceHeight - contentTopInset,
        .surfaceWidth = surfaceWidth,
        .surfaceHeight = surfaceHeight,
        .documentLeftInset = state.outlineWidth,
        .visibleDocumentBottom = layout.totalHeight,
        .showInteractiveElements = false,
    });

    SkPixmap pixels;
    Require(surface->peekPixels(&pixels),
            "complex-text outline surface should expose raster pixels");
    const float arabicRowTop = contentTopInset + mdviewer::kOutlineTopPadding +
        mdviewer::kOutlineItemHeight;
    bool foundRightAlignedArabicInk = false;
    for (int y = static_cast<int>(std::floor(arabicRowTop));
         y < static_cast<int>(std::ceil(arabicRowTop + mdviewer::kOutlineItemHeight));
         ++y) {
        for (int x = 520; x < 606; ++x) {
            if (pixels.getColor(x, y) != palette.menuBackground) {
                foundRightAlignedArabicInk = true;
                break;
            }
        }
    }
    Require(foundRightAlignedArabicInk,
            "Arabic outline text should render visibly from the row's right edge");
}

void HitTestingUsesOnlyClosestLine() {
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
            .visualWidth = 40.0f,
            .caretStops = {
                {line.textStart, 0.0f},
                {line.textStart + 2, 20.0f},
                {line.textStart + 4, 40.0f},
            },
        });
        block.lines.push_back(std::move(line));
    }
    layout.blocks.push_back(std::move(block));

    const auto hit = mdviewer::HitTestDocument(
        layout, 0.0f, 30.0f, 30.0f, 2035.0f, {});
    Require(hit.valid, "hit testing should find the closest visible line");
    RequireEqual(hit.position, static_cast<size_t>(402), "hit testing should use the target line position");
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

void FrontMatterAndMarkdownExtensions() {
    Require(mdviewer::IsMarkdownFile("document.mdown"), ".mdown should be recognized as Markdown");
    Require(mdviewer::IsMarkdownFile("document.MKD"), ".mkd recognition should be case-insensitive");
    Require(!mdviewer::IsMarkdownFile("document.mdx"), ".mdx should remain an explicit future fallback mode");

    TempDir temp;
    const fs::path mdownPath = temp.Path() / "document.mdown";
    const fs::path mkdPath = temp.Path() / "document.mkd";
    WriteText(mdownPath, "# MDown\n");
    WriteText(mkdPath, "# MKD\n");
    Require(mdviewer::LoadDocumentFromPath(mdownPath).status == mdviewer::DocumentLoadStatus::Success,
            ".mdown files should load through the Markdown parser");
    Require(mdviewer::LoadDocumentFromPath(mkdPath).status == mdviewer::DocumentLoadStatus::Success,
            ".mkd files should load through the Markdown parser");

    const mdviewer::DocumentModel yaml = mdviewer::MarkdownParser::Parse(
        "---\n"
        "title: Example Document\n"
        "author: Ada Lovelace\n"
        "tags:\n"
        "  - docs\n"
        "---\n"
        "# Body\n");
    Require(yaml.blocks.size() >= 2, "YAML front matter should precede the Markdown body");
    Require(yaml.blocks.front().type == mdviewer::BlockType::Metadata, "YAML front matter should become a metadata block");
    RequireEqual(yaml.blocks.front().metadataFormat, std::string("YAML"), "YAML metadata format should be retained");
    const std::string yamlMetadataText = MergeInlineRunText(yaml.blocks.front().inlineRuns);
    Require(yamlMetadataText.find("Example Document") != std::string::npos,
            "YAML title should remain visible without a field label");
    Require(yamlMetadataText.find("Ada Lovelace") != std::string::npos,
            "common metadata should use the compact unlabeled row");
    Require(yamlMetadataText.find("docs") != std::string::npos,
            "YAML list metadata should be flattened into tag pills");
    Require(yamlMetadataText.find("Title") == std::string::npos &&
                yamlMetadataText.find("Author") == std::string::npos &&
                yamlMetadataText.find("Tags") == std::string::npos,
            "the metadata row should not repeat field labels");
    Require(yaml.blocks.front().inlineRuns.front().formatting == mdviewer::InlineFormatting::Strong,
            "the metadata title should be emphasized");
    Require(std::any_of(yaml.blocks.front().inlineRuns.begin(), yaml.blocks.front().inlineRuns.end(), [](const auto& run) {
        return run.metadataRole == mdviewer::MetadataRunRole::DotSeparator;
    }), "title and author should be separated by a centered dot");
    Require(std::any_of(yaml.blocks.front().inlineRuns.begin(), yaml.blocks.front().inlineRuns.end(), [](const auto& run) {
        return run.metadataRole == mdviewer::MetadataRunRole::Tag;
    }), "metadata tags should retain their pill role");
    Require(yaml.blocks[1].type == mdviewer::BlockType::Heading1, "Markdown after YAML front matter should parse normally");

    const mdviewer::DocumentModel toml = mdviewer::MarkdownParser::Parse(
        "+++\n"
        "title = \"TOML Document\"\n"
        "author = \"Grace Hopper\"\n"
        "+++\n"
        "Body text.\n");
    Require(!toml.blocks.empty() && toml.blocks.front().type == mdviewer::BlockType::Metadata,
            "TOML front matter should become a metadata block");
    RequireEqual(toml.blocks.front().metadataFormat, std::string("TOML"), "TOML metadata format should be retained");

    const mdviewer::DocumentModel json = mdviewer::MarkdownParser::Parse(
        "{\n"
        "  \"title\": \"JSON Document\",\n"
        "  \"author\": \"Margaret Hamilton\",\n"
        "  \"extra\": {\"enabled\": true}\n"
        "}\n"
        "# JSON Body\n");
    Require(json.blocks.size() >= 2 && json.blocks.front().type == mdviewer::BlockType::Metadata,
            "JSON with typical metadata keys should become a metadata block");
    RequireEqual(json.blocks.front().metadataFormat, std::string("JSON"), "JSON metadata format should be retained");
    Require(json.blocks[1].type == mdviewer::BlockType::Heading1, "Markdown after JSON front matter should parse normally");
    Require(MergeInlineRunText(json.blocks.front().inlineRuns).find("extra") == std::string::npos,
            "non-common JSON metadata should stay hidden from the compact bar");

    const mdviewer::DocumentModel ordinaryJson = mdviewer::MarkdownParser::Parse(
        "{\"payload\": {\"value\": 42}}\n\nFollowing text.\n");
    Require(ordinaryJson.blocks.empty() || ordinaryJson.blocks.front().type != mdviewer::BlockType::Metadata,
            "an arbitrary leading JSON object should not be mistaken for metadata");

    const mdviewer::DocumentModel thematicBreak = mdviewer::MarkdownParser::Parse(
        "---\n\nOrdinary document after a thematic break.\n");
    Require(thematicBreak.blocks.empty() || thematicBreak.blocks.front().type != mdviewer::BlockType::Metadata,
            "an ordinary opening thematic break should remain Markdown");

    const mdviewer::DocumentModel hiddenOnly = mdviewer::MarkdownParser::Parse(
        "---\n"
        "description: Internal metadata only\n"
        "custom_build_flag: true\n"
        "---\n"
        "# Visible Body\n");
    Require(!hiddenOnly.blocks.empty() && hiddenOnly.blocks.front().type == mdviewer::BlockType::Heading1,
            "front matter without common fields should be hidden while the body remains visible");

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    const auto typefaces = MakeTestTypefaces(fontMgr.get(), typeface.get());
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(yaml, 700.0f, typefaces, 17.0f);
    Require(!layout.blocks.empty() && layout.blocks.front().type == mdviewer::BlockType::Metadata,
            "metadata should have a dedicated layout block");
    Require(layout.blocks.front().bounds.height() > 30.0f, "metadata layout should reserve room for its compact contents");
    RequireNear(layout.blocks.front().lines.front().x, layout.blocks.front().bounds.left(), 0.001f,
                "metadata should align with the document body instead of using inset card padding");
    Require(std::any_of(layout.blocks.front().lines.front().runs.begin(), layout.blocks.front().lines.front().runs.end(), [](const auto& run) {
        return run.metadataRole == mdviewer::MetadataRunRole::Tag && run.visualWidth > 0.0f;
    }), "tag pills should expose their full visual width to rendering and hit testing");

    const auto narrowLayout = mdviewer::LayoutEngine::ComputeLayout(yaml, 260.0f, typefaces, 17.0f);
    Require(narrowLayout.blocks.front().lines.size() > 1,
            "the metadata row should wrap when the viewport is narrow");
    for (const auto& line : narrowLayout.blocks.front().lines) {
        if (!line.runs.empty()) {
            Require(line.runs.front().metadataRole != mdviewer::MetadataRunRole::DotSeparator &&
                        line.runs.front().metadataRole != mdviewer::MetadataRunRole::Divider,
                    "wrapped metadata lines should not begin with a separator");
            Require(line.runs.back().metadataRole != mdviewer::MetadataRunRole::DotSeparator &&
                        line.runs.back().metadataRole != mdviewer::MetadataRunRole::Divider,
                    "wrapped metadata lines should not end with a separator");
        }
    }
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
    const auto typefaces = MakeTestTypefaces(
        fontMgr.get(), regularTypeface.get(), boldTypeface.get(), boldTypeface.get());
    const auto breakLayout = mdviewer::LayoutEngine::ComputeLayout(
        breaks,
        1200.0f,
        typefaces,
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
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    Require(!linkLayout.blocks.empty() && !linkLayout.blocks[0].lines.empty(), "link fixture should create a laid-out line");
    const auto linkHit = mdviewer::HitTestDocument(
        linkLayout,
        0.0f,
        30.0f,
        linkLayout.blocks[0].lines[0].x + 1.0f,
        linkLayout.blocks[0].lines[0].y + 31.0f,
        {});
    RequireEqual(linkHit.url, std::string("https://example.com"), "hit testing should preserve a formatted link target");
    Require(mdviewer::HasFormatting(linkHit.formatting, mdviewer::InlineFormatting::Strong), "hit testing should preserve combined formatting metadata");
}

void BidirectionalMarkdownBaseline() {
    const fs::path fixturePath = SourceRoot() / "test-docs" / "bidi-complex-text.md";
    const std::string source = ReadText(fixturePath);
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(source);

    const mdviewer::Block* listItem = FindModelBlockOfType(document.blocks, mdviewer::BlockType::ListItem);
    Require(listItem != nullptr, "BiDi fixture should retain its nested list-item structure");
    const mdviewer::Block* codeBlock =
        FindModelBlockOfType(listItem->children, mdviewer::BlockType::CodeBlock);
    Require(codeBlock != nullptr, "BiDi fixture should retain the fenced code block nested inside lists");
    RequireEqual(codeBlock->codeLanguage, std::string("cpp"), "nested fenced code should retain its language");

    const std::string expectedCode =
        "#include <string>\n"
        "\n"
        "const char* greeting = \"مرحبا\";\n"
        "const char* farewell = \"להתראות\";\n"
        "// הערה בעברית with value = 123\n"
        "auto result = parse(input[2]);\n"
        "*pointer = value;\n"
        ">literal source text remains visible;\n";
    RequireEqual(
        MergeInlineRunText(codeBlock->inlineRuns),
        expectedCode,
        "nested mixed-direction fenced code should remain source-exact");

    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontManager);
    const auto typefaces = MakeTestTypefaces(fontManager.get(), typeface.get());
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        document,
        1200.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);

    const mdviewer::BlockLayout* laidOutCode =
        FindLayoutBlockOfType(layout.blocks, mdviewer::BlockType::CodeBlock);
    Require(laidOutCode != nullptr, "BiDi fixture should retain nested code through layout");
    Require(
        laidOutCode->codeHighlightStatus == mdviewer::syntax::HighlightStatus::Highlighted,
        "nested C++ code should still receive syntax highlighting");
    const bool hasSyntaxRole = std::any_of(
        laidOutCode->lines.begin(),
        laidOutCode->lines.end(),
        [](const auto& line) {
            return std::any_of(line.runs.begin(), line.runs.end(), [](const auto& run) {
                return run.syntaxRole != mdviewer::SyntaxRole::None;
            });
        });
    Require(hasSyntaxRole, "nested mixed-direction C++ code should retain syntax roles after layout");

    Require(
        layout.plainText.find("English before العربية بعد English 123 (v2.0).") != std::string::npos,
        "mixed LTR/RTL paragraph should remain in logical source order in plainText");
    Require(
        layout.plainText.find(
            "بداية RTL: العربية with strong English, emphasized עברית, نص مشطوب, and English link.") !=
            std::string::npos,
        "formatted RTL/LTR paragraph should preserve logical text order without Markdown markers");
    Require(
        layout.plainText.find("const char* greeting = \"مرحبا\";") != std::string::npos &&
            layout.plainText.find("const char* farewell = \"להתראות\";") != std::string::npos,
        "logical copy/search text should retain Arabic and Hebrew code strings");
}

void ComplexTextRuntimeAvailability() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    Require(fontManager != nullptr, "complex text runtime test requires a font manager");

    const mdviewer::ComplexTextRuntime runtime(fontManager);
    Require(runtime.IsAvailable(), runtime.Diagnostic());
    Require(runtime.Unicode() != nullptr, "complex text runtime should expose ICU Unicode services");
    Require(runtime.Shaper() != nullptr, "complex text runtime should expose the HarfBuzz shaper");
}

void ComplexTextShapingSubsystem() {
    Require(
        mdviewer::ResolveFirstStrongDirection("123 (...) English") ==
            mdviewer::ResolvedTextDirection::LeftToRight,
        "first-strong direction should ignore leading neutral characters and digits");
    Require(
        mdviewer::ResolveFirstStrongDirection("123 (...) العربية English") ==
            mdviewer::ResolvedTextDirection::RightToLeft,
        "first-strong direction should resolve Arabic text as RTL");
    Require(
        mdviewer::ResolveFirstStrongDirection("שלום English") ==
            mdviewer::ResolvedTextDirection::RightToLeft,
        "first-strong direction should resolve Hebrew text as RTL");
    Require(
        mdviewer::ResolveFirstStrongDirection("123 — (...)") ==
            mdviewer::ResolvedTextDirection::LeftToRight,
        "a neutral-only paragraph should default to LTR");

    std::vector<mdviewer::ShapedTextInputRun> inputs;
    inputs.push_back({
        .run = mdviewer::InlineRun{.text = "English"},
        .semanticSpanId = 10,
    });
    inputs.push_back({
        .run = mdviewer::InlineRun{.kind = mdviewer::InlineKind::SoftBreak},
        .semanticSpanId = 11,
    });
    inputs.push_back({
        .run = mdviewer::InlineRun{
            .formatting = mdviewer::InlineFormatting::Strong,
            .text = "العربية",
        },
        .semanticSpanId = 12,
    });
    inputs.push_back({
        .run = mdviewer::InlineRun{.kind = mdviewer::InlineKind::HardBreak},
        .semanticSpanId = 13,
    });
    inputs.push_back({
        .run = mdviewer::InlineRun{
            .kind = mdviewer::InlineKind::Image,
            .text = "fixture image",
            .imageSource = "fixture.svg",
        },
        .semanticSpanId = 14,
        .objectWidth = 42.0f,
        .objectHeight = 24.0f,
    });
    inputs.push_back({
        .run = mdviewer::InlineRun{
            .kind = mdviewer::InlineKind::Math,
            .text = "$x+y$",
            .mathSource = "x+y",
        },
        .semanticSpanId = 15,
        .objectWidth = 35.0f,
        .objectHeight = 20.0f,
    });

    const mdviewer::ShapingParagraphSet built = mdviewer::BuildShapingParagraphs(inputs, 100);
    RequireEqual(built.paragraphs.size(), static_cast<size_t>(2),
                 "hard breaks should split complex-text shaping paragraphs");
    RequireEqual(built.logicalText, std::string("English العربية\n$x+y$"),
                 "soft/hard breaks and atomic objects should preserve logical copy text");
    RequireEqual(built.logicalEnd, static_cast<size_t>(100 + built.logicalText.size()),
                 "logical shaping offsets should remain global UTF-8 byte offsets");
    Require(built.paragraphs.front().endedByHardBreak,
            "the paragraph before a hard break should retain its source-line boundary");
    RequireEqual(built.paragraphs.front().spans[1].text, std::string(" "),
                 "soft breaks should shape and copy as a normal space");
    RequireEqual(built.paragraphs[1].spans.front().logicalLength, static_cast<size_t>(0),
                 "image placeholders should remain zero-length in logical copy text");
    RequireEqual(built.paragraphs[1].spans.front().shapingLength, static_cast<size_t>(3),
                 "atomic objects should use one internal UTF-8 EM SPACE placeholder");
    Require(
        built.paragraphs[1].utf8.find("fixture image") == std::string::npos,
        "image alt text should not leak into the internal shaping buffer");

    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "shaping subsystem regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());
    const mdviewer::ComplexTextRuntime runtime(fontManager);
    Require(runtime.IsAvailable(), runtime.Diagnostic());

    mdviewer::ShapingParagraph oversizedParagraph;
    oversizedParagraph.utf8.assign(mdviewer::kMaximumShapingParagraphBytes + 1, 'a');
    oversizedParagraph.spans.push_back({
        .shapingLength = oversizedParagraph.utf8.size(),
        .logicalLength = oversizedParagraph.utf8.size(),
    });
    mdviewer::ShapedTextLayoutOptions safetyOptions;
    safetyOptions.wrapWidth = 600.0f;
    const auto oversizedResult = mdviewer::ShapeTextParagraph(
        oversizedParagraph, runtime, typefaces, safetyOptions);
    Require(!oversizedResult.success &&
                oversizedResult.diagnostic.find("safety limit") != std::string::npos,
            "a maliciously large single shaping paragraph should fail cleanly");

    std::vector<mdviewer::ShapedTextInputRun> mixedInputs;
    mixedInputs.push_back({
        .run = mdviewer::InlineRun{.text = "Start e\xCC\x81 English "},
        .semanticSpanId = 21,
    });
    mixedInputs.push_back({
        .run = mdviewer::InlineRun{
            .formatting = mdviewer::InlineFormatting::Strong,
            .text = "العربية",
            .linkTarget = "https://example.com",
        },
        .semanticSpanId = 22,
    });
    mixedInputs.push_back({
        .run = mdviewer::InlineRun{.text = " עברית end"},
        .semanticSpanId = 23,
    });
    mixedInputs.push_back({
        .run = mdviewer::InlineRun{
            .kind = mdviewer::InlineKind::Math,
            .text = "$x$",
            .mathSource = "x",
        },
        .semanticSpanId = 24,
        .objectWidth = 30.0f,
        .objectHeight = 38.0f,
    });

    const auto mixedParagraphs = mdviewer::BuildShapingParagraphs(mixedInputs, 500);
    RequireEqual(mixedParagraphs.paragraphs.size(), static_cast<size_t>(1),
                 "inline mixed text should shape as one paragraph");
    mdviewer::ShapedTextLayoutOptions options;
    options.blockType = mdviewer::BlockType::Paragraph;
    options.baseFontSize = mdviewer::kDefaultBaseFontSize;
    options.wrapWidth = 220.0f;
    options.startY = 12.0f;
    const mdviewer::ShapedTextLayoutResult shaped = mdviewer::ShapeTextParagraph(
        mixedParagraphs.paragraphs.front(), runtime, typefaces, options);
    Require(shaped.success, shaped.diagnostic);
    Require(shaped.lines.size() >= 2,
            "the shaping subsystem should wrap mixed text at the requested width");
    Require(shaped.totalHeight > 0.0f && shaped.naturalWidth > 0.0f,
            "shaping should return reusable line and natural-width geometry");

    bool foundRtlRun = false;
    bool foundFallbackTypeface = false;
    bool foundStrongSemanticSpan = false;
    bool foundAtomicMath = false;
    bool combiningSequenceStayedAtomic = false;
    bool foundWrappedLineWithinWidth = false;
    for (const mdviewer::LineLayout& line : shaped.lines) {
        foundWrappedLineWithinWidth = foundWrappedLineWithinWidth ||
            line.width <= options.wrapWidth + 1.0f;
        float previousVisualEnd = 0.0f;
        for (const mdviewer::RunLayout& run : line.runs) {
            Require(run.visualX + 0.01f >= previousVisualEnd,
                    "SkShaper output should be stored in visual run order");
            previousVisualEnd = run.visualX + run.visualWidth;
            if ((run.bidiLevel & 1U) != 0) {
                foundRtlRun = true;
            }
            if (run.semanticSpanId == 22) {
                foundStrongSemanticSpan = true;
                Require(run.linkTarget == "https://example.com",
                        "font/script fallback fragments should retain link semantics");
                if (run.shapedFont.getTypeface() != bold.get()) {
                    foundFallbackTypeface = true;
                }
            }
            if (run.kind == mdviewer::InlineKind::Math) {
                foundAtomicMath = true;
                Require(!run.shaped && run.glyphs.empty(),
                        "atomic math should discard its internal placeholder glyphs");
                RequireNear(run.visualWidth, 30.0f, 0.01f,
                            "atomic math should retain its exact requested visual width");
                Require(run.caretStops.size() == 2,
                        "atomic math should expose only logical start/end carets");
            } else {
                Require(run.shaped && !run.glyphs.empty(),
                        "normal complex text should retain shaped glyph data");
                RequireEqual(run.glyphs.size(), run.glyphPositions.size(),
                             "every shaped glyph should retain a visual position");
                RequireEqual(run.glyphs.size(), run.glyphClusters.size(),
                             "every shaped glyph should map back to a UTF-8 cluster");
            }
            const size_t combining = run.text.find("e\xCC\x81");
            if (combining != std::string::npos) {
                const size_t unsafePosition = run.textStart + combining + 1;
                combiningSequenceStayedAtomic = std::none_of(
                    run.caretStops.begin(), run.caretStops.end(), [&](const auto& stop) {
                        return stop.textPosition == unsafePosition;
                    });
            }
        }
        if (std::abs(line.width - previousVisualEnd) > 1.0f) {
            std::ostringstream details;
            details << "shaped line width should match its final visual advance: line="
                    << line.width << ", edge=" << previousVisualEnd;
            for (const auto& run : line.runs) {
                details << " [kind=" << static_cast<int>(run.kind)
                        << ", x=" << run.visualX << ", width=" << run.visualWidth << ']';
            }
            throw TestFailure(details.str());
        }
    }
    Require(foundRtlRun, "mixed Arabic/Hebrew shaping should emit an RTL bidi run");
    Require(foundWrappedLineWithinWidth,
            "the shaper should wrap at safe opportunities while permitting unbreakable overflow");
    Require(foundStrongSemanticSpan, "shaping should preserve semantic Markdown span ids");
    const bool boldFaceNeedsArabicFallback = bold->unicharToGlyph(0x0627) == 0;
    Require(!boldFaceNeedsArabicFallback || foundFallbackTypeface,
            "missing Arabic glyphs should use system fallback while retaining strong semantics");
    Require(foundAtomicMath, "inline math should participate as an atomic shaping object");
    Require(combiningSequenceStayedAtomic,
            "HarfBuzz caret geometry should not split a combining grapheme sequence");

    std::vector<mdviewer::ShapedTextInputRun> rtlInputs{{
        .run = mdviewer::InlineRun{.text = "العربية before English 123"},
        .semanticSpanId = 25,
    }};
    const auto rtlParagraphs = mdviewer::BuildShapingParagraphs(rtlInputs, 700);
    const auto shapedRtl = mdviewer::ShapeTextParagraph(
        rtlParagraphs.paragraphs.front(), runtime, typefaces, options);
    Require(shapedRtl.success &&
                shapedRtl.direction == mdviewer::ResolvedTextDirection::RightToLeft,
            "an Arabic-first paragraph should shape with an RTL base level");
    Require(std::all_of(
        shapedRtl.lines.begin(), shapedRtl.lines.end(), [](const auto& line) {
            return line.direction == mdviewer::ResolvedTextDirection::RightToLeft;
        }),
        "every wrapped line should retain its paragraph base direction");
    const mdviewer::RunLayout* leadingRtlRun = nullptr;
    for (const auto& line : shapedRtl.lines) {
        for (const auto& run : line.runs) {
            if ((run.bidiLevel & 1U) != 0 && run.text.find("العربية") != std::string::npos) {
                leadingRtlRun = &run;
                break;
            }
        }
        if (leadingRtlRun != nullptr) {
            break;
        }
    }
    Require(leadingRtlRun != nullptr, "RTL-first shaping should retain an odd-level Arabic run");
    const auto rtlStartCaret = std::find_if(
        leadingRtlRun->caretStops.begin(), leadingRtlRun->caretStops.end(),
        [&](const auto& stop) { return stop.textPosition == leadingRtlRun->textStart; });
    Require(rtlStartCaret != leadingRtlRun->caretStops.end(),
            "RTL shaped runs should expose their logical-start caret");
    RequireNear(
        rtlStartCaret->x,
        leadingRtlRun->visualX + leadingRtlRun->visualWidth,
        0.5f,
        "the logical start of an RTL run should map to its right visual edge");

    const std::string arabicCluster = "نَ";
    std::string unbrokenArabic;
    for (int index = 0; index < 80; ++index) {
        unbrokenArabic += arabicCluster;
    }
    const auto unbrokenParagraphs = mdviewer::BuildShapingParagraphs({
        {.run = mdviewer::InlineRun{.text = unbrokenArabic}},
    });
    mdviewer::ShapedTextLayoutOptions unbrokenOptions = options;
    unbrokenOptions.wrapWidth = 54.0f;
    const auto shapedUnbroken = mdviewer::ShapeTextParagraph(
        unbrokenParagraphs.paragraphs.front(), runtime, typefaces, unbrokenOptions);
    Require(shapedUnbroken.success,
            "a long unbroken Arabic combining sequence should shape successfully");
    for (const mdviewer::LineLayout& line : shapedUnbroken.lines) {
        const size_t lineEnd = line.textStart + line.textLength;
        Require(
            line.textStart % arabicCluster.size() == 0 &&
                lineEnd % arabicCluster.size() == 0,
            "complex-script wrapping must not split an Arabic grapheme cluster");
        for (const mdviewer::RunLayout& run : line.runs) {
            for (const mdviewer::CaretStop& stop : run.caretStops) {
                Require(stop.textPosition % arabicCluster.size() != 2,
                        "Arabic combining marks must not receive an independent caret stop");
            }
        }
    }

    const auto emptyLines = mdviewer::BuildShapingParagraphs({
        {.run = mdviewer::InlineRun{.kind = mdviewer::InlineKind::HardBreak}},
        {.run = mdviewer::InlineRun{.kind = mdviewer::InlineKind::HardBreak}},
    }, 800);
    RequireEqual(emptyLines.paragraphs.size(), static_cast<size_t>(2),
                 "consecutive source breaks should retain empty shaping paragraphs");
    RequireEqual(emptyLines.logicalText, std::string("\n\n"),
                 "empty source lines should preserve both logical newlines");
    const auto shapedEmpty = mdviewer::ShapeTextParagraph(
        emptyLines.paragraphs.front(), runtime, typefaces, options);
    Require(shapedEmpty.success && shapedEmpty.lines.size() == 1 && shapedEmpty.totalHeight > 0.0f,
            "an empty shaping paragraph should still reserve one line height");

    std::vector<mdviewer::ShapedTextInputRun> codeInputs{{
        .run = mdviewer::InlineRun{
            .syntaxRole = mdviewer::SyntaxRole::String,
            .text = "const char* value = \"العربية\";",
        },
        .semanticSpanId = 31,
    }};
    const auto codeParagraphs = mdviewer::BuildShapingParagraphs(codeInputs, 900);
    mdviewer::ShapedTextLayoutOptions codeOptions = options;
    codeOptions.blockType = mdviewer::BlockType::CodeBlock;
    codeOptions.wrapWidth = 1000000.0f;
    const auto shapedCode = mdviewer::ShapeTextParagraph(
        codeParagraphs.paragraphs.front(), runtime, typefaces, codeOptions);
    Require(shapedCode.success && shapedCode.lines.size() == 1,
            "one preserved code source line should shape without wrapping");
    Require(shapedCode.naturalWidth > 0.0f,
            "unwrapped shaped code should report its horizontal-scroll width");
    Require(std::all_of(
        shapedCode.lines.front().runs.begin(), shapedCode.lines.front().runs.end(),
        [](const auto& run) { return run.syntaxRole == mdviewer::SyntaxRole::String; }),
        "syntax metadata should survive font and bidi run fragmentation");
}

void SharedTextGeometryModel() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontManager);
    const auto typefaces = MakeTestTypefaces(fontManager.get(), typeface.get());
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "Legacy e\xCC\x81 cluster and **formatted text**.\n\n"
            "Inline math $x + y$ remains atomic.\n"),
        900.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);

    bool foundCombiningCluster = false;
    for (const mdviewer::BlockLayout& block : layout.blocks) {
        Require(
            block.direction == mdviewer::ResolvedTextDirection::LeftToRight,
            "legacy blocks should retain an explicit LTR direction");
        for (const mdviewer::LineLayout& line : block.lines) {
            Require(line.width >= 0.0f, "every legacy line should expose its visual width");
            Require(
                line.direction == mdviewer::ResolvedTextDirection::LeftToRight,
                "legacy lines should retain an explicit LTR direction");
            float previousVisualEnd = 0.0f;
            for (const mdviewer::RunLayout& run : line.runs) {
                Require(run.semanticSpanId != 0, "every visible legacy run should retain a semantic span id");
                Require(run.visualX + 0.01f >= previousVisualEnd,
                        "legacy visual runs should be stored in line-local visual order");
                Require(run.visualWidth >= 0.0f, "every visual run should carry its layout width");
                previousVisualEnd = run.visualX + run.visualWidth;

                if (run.kind == mdviewer::InlineKind::Text && !run.text.empty()) {
                    Require(!run.caretStops.empty(), "legacy text should expose safe caret stops");
                    RequireEqual(run.caretStops.front().textPosition, run.textStart,
                                 "legacy caret geometry should begin at the logical run start");
                    RequireEqual(run.caretStops.back().textPosition, run.textStart + run.text.size(),
                                 "legacy caret geometry should end at the logical run end");
                }

                const size_t combining = run.text.find("e\xCC\x81");
                if (combining != std::string::npos) {
                    foundCombiningCluster = true;
                    const size_t unsafePosition = run.textStart + combining + 1;
                    Require(
                        std::none_of(run.caretStops.begin(), run.caretStops.end(), [&](const auto& stop) {
                            return stop.textPosition == unsafePosition;
                        }),
                        "legacy caret geometry should not split a combining sequence");
                }
            }
            RequireNear(line.width, previousVisualEnd, 0.01f,
                        "legacy line width should match its final visual run edge");
        }
    }
    Require(foundCombiningCluster, "geometry regression should exercise a combining sequence");

    mdviewer::RunLayout rtlRun{};
    rtlRun.kind = mdviewer::InlineKind::Text;
    rtlRun.text = "abcd";
    rtlRun.textStart = 4;
    rtlRun.visualX = 0.0f;
    rtlRun.visualWidth = 40.0f;
    rtlRun.bidiLevel = 1;
    rtlRun.caretStops = {{4, 40.0f}, {6, 20.0f}, {8, 0.0f}};

    mdviewer::RunLayout ltrRun{};
    ltrRun.kind = mdviewer::InlineKind::Text;
    ltrRun.text = "abcd";
    ltrRun.textStart = 0;
    ltrRun.visualX = 60.0f;
    ltrRun.visualWidth = 40.0f;
    ltrRun.caretStops = {{0, 60.0f}, {2, 80.0f}, {4, 100.0f}};

    RequireEqual(mdviewer::HitTestTextRun(rtlRun, 1.0f), static_cast<size_t>(8),
                 "RTL hit testing should map the left visual edge to the logical end");
    RequireEqual(mdviewer::HitTestTextRun(rtlRun, 39.0f), static_cast<size_t>(4),
                 "RTL hit testing should map the right visual edge to the logical start");

    mdviewer::LineLayout reorderedLine{};
    reorderedLine.x = 10.0f;
    reorderedLine.runs = {rtlRun, ltrRun};
    const std::vector<SkRect> discontiguous = mdviewer::GetTextRangeRects(
        reorderedLine, 2, 6, 5.0f, 15.0f);
    RequireEqual(discontiguous.size(), static_cast<size_t>(2),
                 "a logical range split by visual reordering should produce two rectangles");
    RequireNear(discontiguous[0].left(), 30.0f, 0.01f,
                "RTL selection geometry should use line-local caret positions");
    RequireNear(discontiguous[1].left(), 90.0f, 0.01f,
                "LTR selection geometry should remain visually discontiguous");

    mdviewer::RunLayout ligatureRun{};
    ligatureRun.kind = mdviewer::InlineKind::Text;
    ligatureRun.text = "ffi";
    ligatureRun.textStart = 10;
    ligatureRun.visualWidth = 30.0f;
    ligatureRun.caretStops = {{10, 0.0f}, {13, 30.0f}};
    RequireEqual(mdviewer::HitTestTextRun(ligatureRun, 16.0f), static_cast<size_t>(13),
                 "hit testing should not invent a caret inside a ligature cluster");
    mdviewer::LineLayout ligatureLine{};
    ligatureLine.runs.push_back(ligatureRun);
    const auto ligatureRects = mdviewer::GetTextRangeRects(ligatureLine, 11, 12, 0.0f, 10.0f);
    RequireEqual(ligatureRects.size(), static_cast<size_t>(1),
                 "a partial logical ligature range should snap to one safe cluster rectangle");
    RequireNear(ligatureRects.front().width(), 30.0f, 0.01f,
                "ligature selection should snap outward to its safe caret stops");

    mdviewer::RunLayout mathRun{};
    mathRun.kind = mdviewer::InlineKind::Math;
    mathRun.text = "x+y";
    mathRun.textStart = 20;
    mathRun.visualX = 5.0f;
    mathRun.visualWidth = 30.0f;
    RequireEqual(mdviewer::HitTestTextRun(mathRun, 6.0f), static_cast<size_t>(20),
                 "math hit testing should treat the formula as an atomic logical run");
    RequireEqual(mdviewer::HitTestTextRun(mathRun, 34.0f), static_cast<size_t>(23),
                 "math hit testing should expose only the atomic end boundary");

    mdviewer::RunLayout imageRun{};
    imageRun.kind = mdviewer::InlineKind::Image;
    imageRun.textStart = 30;
    imageRun.visualX = 5.0f;
    imageRun.visualWidth = 20.0f;
    RequireEqual(mdviewer::HitTestTextRun(imageRun, 24.0f), static_cast<size_t>(30),
                 "zero-length image placeholders should resolve to their logical anchor");
}

void LayoutOwnedDocumentHitTesting() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(
        fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "layout-owned hit testing requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());
    const std::string source =
        "العربية قبل [رابط عربي](https://example.com/rtl) English עברית نهاية.\n";
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(source),
        900.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    Require(layout.blocks.size() == 1 && layout.blocks.front().lines.size() == 1,
            "RTL hit-test fixture should remain on one visual line");
    const mdviewer::LineLayout& line = layout.blocks.front().lines.front();
    const mdviewer::RunLayout* rtlRun = nullptr;
    const mdviewer::RunLayout* linkRun = nullptr;
    for (const mdviewer::RunLayout& run : line.runs) {
        if (rtlRun == nullptr && (run.bidiLevel & 1U) != 0 && run.visualWidth > 2.0f) {
            rtlRun = &run;
        }
        if (linkRun == nullptr && run.linkTarget == "https://example.com/rtl") {
            linkRun = &run;
        }
    }
    Require(rtlRun != nullptr && linkRun != nullptr,
            "RTL hit-test fixture should retain odd-level text and link fragments");

    constexpr float contentTop = 30.0f;
    const float viewportY = line.y + contentTop + (line.height * 0.5f);
    const float rtlLeft = line.x + rtlRun->visualX;
    const float rtlRight = rtlLeft + rtlRun->visualWidth;
    const mdviewer::DocumentTextHit visualRightHit = mdviewer::HitTestDocument(
        layout, 0.0f, contentTop, rtlRight - 0.25f, viewportY, {});
    const mdviewer::DocumentTextHit visualLeftHit = mdviewer::HitTestDocument(
        layout, 0.0f, contentTop, rtlLeft + 0.25f, viewportY, {});
    RequireEqual(
        visualRightHit.position,
        rtlRun->textStart,
        "the right visual edge of an RTL run should map to its logical start");
    RequireEqual(
        visualLeftHit.position,
        rtlRun->textStart + rtlRun->text.size(),
        "the left visual edge of an RTL run should map to its logical end");
    Require(std::any_of(
        rtlRun->caretStops.begin(), rtlRun->caretStops.end(), [&](const auto& stop) {
            return stop.textPosition == visualRightHit.position;
        }),
        "RTL document hits should snap to a safe shaped caret boundary");
    Require(std::any_of(
        rtlRun->caretStops.begin(), rtlRun->caretStops.end(), [&](const auto& stop) {
            return stop.textPosition == visualLeftHit.position;
        }),
        "RTL document hits must not invent an interior ligature caret");

    const float linkCenter = line.x + linkRun->visualX + (linkRun->visualWidth * 0.5f);
    const mdviewer::DocumentTextHit linkHit = mdviewer::HitTestDocument(
        layout, 0.0f, contentTop, linkCenter, viewportY, {});
    RequireEqual(linkHit.url, std::string("https://example.com/rtl"),
                 "a visual RTL link fragment should retain its logical link target");
    RequireEqual(linkHit.linkTarget, std::string("https://example.com/rtl"),
                 "RTL link context semantics should remain attached to the hit fragment");
    const mdviewer::DocumentTextHit outsideLineHit = mdviewer::HitTestDocument(
        layout, 0.0f, contentTop, line.x - 20.0f, viewportY, {});
    Require(outsideLineHit.valid && outsideLineHit.url.empty(),
            "free space outside a visual line should provide a caret without activating a link");
    const mdviewer::DocumentTextHit belowLinkHit = mdviewer::HitTestDocument(
        layout,
        0.0f,
        contentTop,
        linkCenter,
        line.y + contentTop + line.height + 10.0f,
        {});
    Require(belowLinkHit.valid && belowLinkHit.url.empty(),
            "vertical free space near a link should provide a caret without link semantics");

    mdviewer::AppState selectionState;
    selectionState.docLayout = layout;
    mdviewer::BeginSelection(
        selectionState,
        mdviewer::InteractionTextHit{
            .position = visualRightHit.position,
            .valid = visualRightHit.valid,
        },
        false,
        0,
        0);
    mdviewer::UpdateSelectionFromHit(
        selectionState,
        mdviewer::InteractionTextHit{
            .position = visualLeftHit.position,
            .valid = visualLeftHit.valid,
        },
        line.y,
        500.0f,
        0.0f);
    const size_t selectionStart = mdviewer::GetSelectionStart(selectionState);
    const size_t selectionEnd = mdviewer::GetSelectionEnd(selectionState);
    RequireEqual(
        selectionState.docLayout.plainText.substr(
            selectionStart, selectionEnd - selectionStart),
        rtlRun->text,
        "dragging across an RTL run should still copy its exact logical UTF-8 substring");

    mdviewer::AppState searchState;
    searchState.docLayout = layout;
    mdviewer::OpenSearch(searchState);
    mdviewer::InsertSearchText(searchState, "עברית");
    RequireEqual(searchState.searchMatches.size(), static_cast<size_t>(1),
                 "exact Hebrew search should remain logical after hit-test migration");
    const auto& searchMatch = searchState.searchMatches.front();
    const std::vector<SkRect> searchRects = mdviewer::GetTextRangeRects(
        line,
        searchMatch.first,
        searchMatch.second,
        line.y,
        line.y + line.height);
    Require(!searchRects.empty(),
            "an exact Hebrew search should resolve to shaped visual highlight geometry");

    mdviewer::DocumentLayout scrolledLayout;
    mdviewer::BlockLayout scrolledBlock;
    scrolledBlock.type = mdviewer::BlockType::CodeBlock;
    scrolledBlock.usesHorizontalScrollOffset = true;
    scrolledBlock.horizontalScrollOwnerTextStart = 77;
    mdviewer::LineLayout scrolledLine;
    scrolledLine.x = 20.0f;
    scrolledLine.y = 10.0f;
    scrolledLine.height = 20.0f;
    scrolledLine.textLength = 4;
    scrolledLine.runs.push_back(mdviewer::RunLayout{
        .text = "code",
        .visualX = 60.0f,
        .visualWidth = 40.0f,
        .caretStops = {{0, 60.0f}, {4, 100.0f}},
    });
    scrolledBlock.lines.push_back(std::move(scrolledLine));
    scrolledLayout.blocks.push_back(std::move(scrolledBlock));
    const mdviewer::DocumentTextHit scrolledHit = mdviewer::HitTestDocument(
        scrolledLayout,
        0.0f,
        0.0f,
        50.25f,
        20.0f,
        mdviewer::HitTestCallbacks{
            .get_block_horizontal_scroll = [](const auto&) { return 30.0f; },
        });
    RequireEqual(scrolledHit.position, static_cast<size_t>(0),
                 "block-local horizontal scrolling should offset shared visual hit geometry");
}

void ShapedLayoutEngineIntegration() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "shaped LayoutEngine regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());

    mdviewer::DocumentModel directionalDocument;
    directionalDocument.blocks = {
        mdviewer::Block{
            .type = mdviewer::BlockType::Paragraph,
            .inlineRuns = {{.text = "English before العربية after"}},
        },
        mdviewer::Block{
            .type = mdviewer::BlockType::Paragraph,
            .inlineRuns = {{.text = "العربية before English 123"}},
        },
        mdviewer::Block{
            .type = mdviewer::BlockType::Paragraph,
            .align = mdviewer::TextAlign::Left,
            .inlineRuns = {{.text = "עברית explicitly left"}},
        },
        mdviewer::Block{
            .type = mdviewer::BlockType::Paragraph,
            .align = mdviewer::TextAlign::Right,
            .inlineRuns = {{.text = "English explicitly right"}},
        },
    };
    const mdviewer::DocumentLayout directionalLayout = mdviewer::LayoutEngine::ComputeLayout(
        directionalDocument, 760.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    RequireEqual(directionalLayout.blocks.size(), static_cast<size_t>(4),
                 "direction fixture should retain all paragraph blocks");

    const auto& ltrBlock = directionalLayout.blocks[0];
    const auto& rtlBlock = directionalLayout.blocks[1];
    const auto& explicitLeftBlock = directionalLayout.blocks[2];
    const auto& explicitRightBlock = directionalLayout.blocks[3];
    Require(ltrBlock.direction == mdviewer::ResolvedTextDirection::LeftToRight &&
                rtlBlock.direction == mdviewer::ResolvedTextDirection::RightToLeft,
            "LayoutEngine should retain first-strong block directions");
    RequireNear(ltrBlock.lines.front().x, ltrBlock.bounds.left(), 0.01f,
                "default LTR paragraphs should align to the content left");
    RequireNear(
        rtlBlock.lines.front().x + rtlBlock.lines.front().width,
        rtlBlock.bounds.right(),
        0.01f,
        "default RTL paragraphs should align to the content right");
    RequireNear(explicitLeftBlock.lines.front().x, explicitLeftBlock.bounds.left(), 0.01f,
                "explicit left alignment should override RTL base direction");
    RequireNear(
        explicitRightBlock.lines.front().x + explicitRightBlock.lines.front().width,
        explicitRightBlock.bounds.right(),
        0.01f,
        "explicit right alignment should override LTR base direction");

    bool foundShapedText = false;
    bool foundRtlRun = false;
    for (const auto& block : directionalLayout.blocks) {
        for (const auto& line : block.lines) {
            for (const auto& run : line.runs) {
                if (run.kind == mdviewer::InlineKind::Text) {
                    foundShapedText = foundShapedText ||
                        (run.shaped && !run.glyphs.empty() && !run.glyphClusters.empty());
                    foundRtlRun = foundRtlRun || (run.bidiLevel & 1U) != 0;
                }
            }
        }
    }
    Require(foundShapedText, "normal LayoutEngine text should now retain HarfBuzz glyph geometry");
    Require(foundRtlRun, "mixed LayoutEngine paragraphs should retain odd-level visual runs");

    const mdviewer::DocumentModel formattedDocument = mdviewer::MarkdownParser::Parse(
        "العربية **نص قوي** *نص مائل* ~~نص محذوف~~ `شفرة` "
        "<kbd>مفتاح</kbd> H<sub>٢</sub> x<sup>٢</sup>.\n");
    const auto formattedLayout = mdviewer::LayoutEngine::ComputeLayout(
        formattedDocument, 760.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    const auto hasShapedFormatting = [&](mdviewer::InlineFormatting formatting) {
        return std::any_of(
            formattedLayout.blocks.front().lines.begin(),
            formattedLayout.blocks.front().lines.end(),
            [&](const mdviewer::LineLayout& line) {
                return std::any_of(line.runs.begin(), line.runs.end(), [&](const mdviewer::RunLayout& run) {
                    return run.shaped && mdviewer::HasFormatting(run.formatting, formatting);
                });
            });
    };
    for (const mdviewer::InlineFormatting formatting : {
             mdviewer::InlineFormatting::Strong,
             mdviewer::InlineFormatting::Emphasis,
             mdviewer::InlineFormatting::Strikethrough,
             mdviewer::InlineFormatting::Code,
             mdviewer::InlineFormatting::Keyboard,
             mdviewer::InlineFormatting::Subscript,
             mdviewer::InlineFormatting::Superscript,
         }) {
        Require(hasShapedFormatting(formatting),
                "complex-text shaping should preserve every supported inline formatting flag");
    }

    mdviewer::DocumentModel invariantDocument;
    invariantDocument.blocks.push_back(mdviewer::Block{
        .type = mdviewer::BlockType::Paragraph,
        .inlineRuns = {
            {.text = "soft"},
            {.kind = mdviewer::InlineKind::SoftBreak},
            {.text = "join"},
            {.kind = mdviewer::InlineKind::HardBreak},
            {
                .kind = mdviewer::InlineKind::Image,
                .text = "hidden replacement text",
                .imageSource = "fixture.png",
            },
            {.text = " after image "},
            {
                .kind = mdviewer::InlineKind::Math,
                .text = "$x+y$",
                .mathSource = "x+y",
            },
        },
    });
    const auto invariantLayout = mdviewer::LayoutEngine::ComputeLayout(
        invariantDocument,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize,
        [](const std::string&) { return std::pair<float, float>{48.0f, 24.0f}; });
    RequireEqual(
        invariantLayout.plainText,
        std::string("soft join\n after image $x+y$"),
        "shaped layout should preserve soft/hard breaks, math text, and zero-length images exactly");
    Require(invariantLayout.plainText.find("hidden replacement") == std::string::npos,
            "internal object placeholders and image alt text must not leak into copy/search text");
    Require(invariantLayout.blocks.front().lines.size() >= 2,
            "a hard break should still force a shaped visual line boundary");

    const mdviewer::DocumentModel mixedListDocument = mdviewer::MarkdownParser::Parse(
        "- English list item\n"
        "- عنصر عربي في القائمة\n");
    const auto mixedListLayout = mdviewer::LayoutEngine::ComputeLayout(
        mixedListDocument, 760.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    std::vector<const mdviewer::BlockLayout*> listItems;
    const auto collectListItems = [&](const auto& self, const auto& blocks) -> void {
        for (const auto& block : blocks) {
            if (block.type == mdviewer::BlockType::ListItem) {
                listItems.push_back(&block);
            }
            self(self, block.children);
        }
    };
    collectListItems(collectListItems, mixedListLayout.blocks);
    RequireEqual(listItems.size(), static_cast<size_t>(2),
                 "mixed-direction list fixture should retain both list items");
    const mdviewer::BlockLayout* ltrItem = listItems[0];
    const mdviewer::BlockLayout* rtlItem = listItems[1];
    Require(ltrItem->direction == mdviewer::ResolvedTextDirection::LeftToRight &&
                rtlItem->direction == mdviewer::ResolvedTextDirection::RightToLeft,
            "each list item should resolve its own flow direction");
    const mdviewer::DocumentHorizontalInsets documentInsets =
        mdviewer::GetDocumentHorizontalInsets(760.0f);
    const float documentLeft = documentInsets.left;
    const float documentRight = 760.0f - documentInsets.right;
    RequireNear(ltrItem->bounds.left() - documentLeft, 40.0f, 0.01f,
                "LTR list gutters should preserve their existing left inset");
    RequireNear(documentRight - rtlItem->bounds.right(), 40.0f, 0.01f,
                "RTL list gutters should mirror the same inset on the right");
    RequireNear(ltrItem->lines.front().x, ltrItem->bounds.left(), 0.01f,
                "LTR list text should begin after its left gutter");
    RequireNear(
        rtlItem->lines.front().x + rtlItem->lines.front().width,
        rtlItem->bounds.right(),
        0.01f,
        "RTL list text should end before its right gutter");

    const mdviewer::DocumentModel nestedDirectionalListDocument = mdviewer::MarkdownParser::Parse(
        "- عنصر عربي رئيسي\n"
        "  1. English child under RTL parent\n\n"
        "- English parent item\n"
        "  1. פריט עברי מקונן\n");
    const auto nestedDirectionalListLayout = mdviewer::LayoutEngine::ComputeLayout(
        nestedDirectionalListDocument, 760.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    struct NestedListItem {
        const mdviewer::BlockLayout* block = nullptr;
        int depth = 0;
    };
    std::vector<NestedListItem> nestedItems;
    const auto collectNestedItems = [&](const auto& self, const auto& blocks, int depth) -> void {
        for (const auto& block : blocks) {
            if (block.type == mdviewer::BlockType::ListItem) {
                nestedItems.push_back({&block, depth});
            }
            self(self, block.children, depth + 1);
        }
    };
    collectNestedItems(collectNestedItems, nestedDirectionalListLayout.blocks, 0);
    RequireEqual(nestedItems.size(), static_cast<size_t>(4),
                 "mixed nested list fixture should retain both directional parent-child pairs");
    Require(
        nestedItems[0].block->direction == mdviewer::ResolvedTextDirection::RightToLeft &&
            nestedItems[1].block->direction == mdviewer::ResolvedTextDirection::LeftToRight &&
            nestedItems[1].depth > nestedItems[0].depth,
        "an LTR child should retain its direction beneath an RTL parent");
    Require(
        nestedItems[2].block->direction == mdviewer::ResolvedTextDirection::LeftToRight &&
            nestedItems[3].block->direction == mdviewer::ResolvedTextDirection::RightToLeft &&
            nestedItems[3].depth > nestedItems[2].depth,
        "an RTL child should retain its direction beneath an LTR parent");
    for (const NestedListItem& nestedItem : nestedItems) {
        Require(
            nestedItem.block->bounds.left() >= documentLeft - 0.01f &&
                nestedItem.block->bounds.right() <= documentRight + 0.01f,
            "mixed-direction nested list content should remain inside the document viewport");
    }

    const mdviewer::DocumentModel codeAndTableDocument = mdviewer::MarkdownParser::Parse(
        "```cpp\n"
        "const char* greeting = \"مرحبا\";\n"
        "const char* farewell = \"להתראות\";\n"
        "```\n\n"
        "| العربية | English | עברית |\n"
        "| --- | --- | --- |\n"
        "| قيمة | value | ערך |\n");
    const auto codeAndTableLayout = mdviewer::LayoutEngine::ComputeLayout(
        codeAndTableDocument, 900.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    const auto& shapedCode = FirstBlockOfType(codeAndTableLayout, mdviewer::BlockType::CodeBlock);
    RequireEqual(shapedCode.lines.size(), static_cast<size_t>(2),
                 "shaped code should preserve each source line without wrapping");
    const float widestCodeLine = std::max_element(
        shapedCode.lines.begin(), shapedCode.lines.end(), [](const auto& left, const auto& right) {
            return left.width < right.width;
        })->width;
    RequireNear(shapedCode.codeContentWidth, widestCodeLine, 0.01f,
                "code horizontal scrolling should use shaped natural width");
    Require(std::all_of(
        shapedCode.lines.begin(), shapedCode.lines.end(), [](const auto& line) {
            return std::all_of(line.runs.begin(), line.runs.end(), [](const auto& run) {
                return run.kind != mdviewer::InlineKind::Text || run.shaped;
            });
        }),
        "syntax-highlighted code fragments should use the shared shaped path");

    const auto& shapedTable = FirstBlockOfType(codeAndTableLayout, mdviewer::BlockType::Table);
    bool foundShapedTableCell = false;
    for (const auto& row : shapedTable.children) {
        for (const auto& cell : row.children) {
            for (const auto& line : cell.lines) {
                foundShapedTableCell = foundShapedTableCell || std::any_of(
                    line.runs.begin(), line.runs.end(), [](const auto& run) { return run.shaped; });
            }
        }
    }
    Require(foundShapedTableCell,
            "table headers and cells should use shaped measurement and visual runs");
    RequireEqual(shapedTable.tableTsv, std::string("العربية\tEnglish\tעברית\r\nقيمة\tvalue\tערך"),
                 "shaped table layout must not change TSV serialization");
    RequireEqual(shapedTable.tableCsv, std::string("العربية,English,עברית\r\nقيمة,value,ערך"),
                 "shaped table layout must not change CSV serialization");
}

void DirectionAwareBlockDecorations() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "directional decoration regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());

    const std::string source =
        "- English list item\n"
        "- عنصر عربي في القائمة\n\n"
        "1. English ordered item\n"
        "2. פריט עברי ממוספר\n\n"
        "- [ ] English task\n"
        "- [x] مهمة عربية\n\n"
        "> English quotation.\n\n"
        "> اقتباس عربي.\n\n"
        "> [!WARNING]\n"
        "> אזהרה בעברית.\n";
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(source);
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        document,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);

    std::vector<const mdviewer::BlockLayout*> listItems;
    std::vector<const mdviewer::BlockLayout*> quotes;
    const auto collectBlocks = [&](const auto& self, const auto& blocks) -> void {
        for (const auto& block : blocks) {
            if (block.type == mdviewer::BlockType::ListItem) {
                listItems.push_back(&block);
            } else if (block.type == mdviewer::BlockType::Blockquote) {
                quotes.push_back(&block);
            }
            self(self, block.children);
        }
    };
    collectBlocks(collectBlocks, layout.blocks);
    RequireEqual(listItems.size(), static_cast<size_t>(6),
                 "directional decoration fixture should retain bullet, ordered, and task items");
    RequireEqual(quotes.size(), static_cast<size_t>(3),
                 "directional decoration fixture should retain quotes and alert");
    for (size_t index = 0; index < listItems.size(); index += 2) {
        Require(listItems[index]->direction == mdviewer::ResolvedTextDirection::LeftToRight &&
                    listItems[index + 1]->direction == mdviewer::ResolvedTextDirection::RightToLeft,
                "each list marker type should use its item's resolved direction");
    }
    Require(listItems[4]->taskListState == mdviewer::TaskListState::Unchecked &&
                listItems[5]->taskListState == mdviewer::TaskListState::Checked,
            "directional decoration fixture should retain both task checkbox states");
    Require(quotes[0]->direction == mdviewer::ResolvedTextDirection::LeftToRight &&
                quotes[1]->direction == mdviewer::ResolvedTextDirection::RightToLeft &&
                quotes[2]->direction == mdviewer::ResolvedTextDirection::RightToLeft,
            "quote and alert decoration should use each block's resolved direction");

    mdviewer::ThemePalette palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark);
    palette.bodyText = palette.windowBackground;
    palette.blockquoteText = palette.windowBackground;
    palette.headingText = palette.windowBackground;
    palette.linkText = palette.windowBackground;
    palette.listMarker = SK_ColorRED;
    palette.blockquoteAccent = SK_ColorGREEN;

    mdviewer::AppState state;
    state.sourceText = source;
    state.docLayout = layout;
    state.outlineCollapsed = true;
    const int renderHeight = static_cast<int>(std::ceil(layout.totalHeight + 20.0f));
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(760, renderHeight));
    Require(surface != nullptr, "directional decorations should create a raster surface");
    surface->getCanvas()->clear(palette.windowBackground);
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = surface->getCanvas(),
        .appState = &state,
        .palette = palette,
        .typefaces = typefaces,
        .baseFontSize = mdviewer::kDefaultBaseFontSize,
        .viewportHeight = static_cast<float>(renderHeight),
        .surfaceWidth = 760.0f,
        .surfaceHeight = static_cast<float>(renderHeight),
        .visibleDocumentBottom = layout.totalHeight,
        .showInteractiveElements = false,
    });
    SkPixmap pixels;
    Require(surface->peekPixels(&pixels),
            "directional decoration surface should expose pixels");

    const auto markerCenterY = [](const mdviewer::BlockLayout& item) {
        const mdviewer::LineLayout* line = nullptr;
        const auto findLine = [&](const auto& self, const mdviewer::BlockLayout& block) -> void {
            if (line != nullptr) {
                return;
            }
            if (!block.lines.empty()) {
                line = &block.lines.front();
                return;
            }
            for (const auto& child : block.children) {
                self(self, child);
            }
        };
        findLine(findLine, item);
        Require(line != nullptr, "list marker fixture should expose a descendant line");
        const float baseline = line->y + line->height - 5.0f;
        return baseline - (line->height * 0.35f);
    };
    const int ltrMarkerX = static_cast<int>(std::round(listItems[0]->bounds.left() - 16.0f));
    const int rtlMarkerX = static_cast<int>(std::round(listItems[1]->bounds.right() + 16.0f));
    RequireEqual(
        pixels.getColor(ltrMarkerX, static_cast<int>(std::round(markerCenterY(*listItems[0])))),
        static_cast<SkColor>(SK_ColorRED),
        "LTR bullets should remain in the left gutter");
    RequireEqual(
        pixels.getColor(rtlMarkerX, static_cast<int>(std::round(markerCenterY(*listItems[1])))),
        static_cast<SkColor>(SK_ColorRED),
        "RTL bullets should render in the mirrored right gutter");
    RequireEqual(
        pixels.getColor(
            static_cast<int>(std::round(listItems[1]->bounds.left() - 16.0f)),
            static_cast<int>(std::round(markerCenterY(*listItems[1])))),
        palette.windowBackground,
        "RTL bullets should no longer leak into the document's left padding");

    const auto hasNonBackgroundPixel = [&](int left, int top, int right, int bottom) {
        for (int y = std::max(top, 0); y < std::min(bottom, pixels.height()); ++y) {
            for (int x = std::max(left, 0); x < std::min(right, pixels.width()); ++x) {
                if (pixels.getColor(x, y) != palette.windowBackground) {
                    return true;
                }
            }
        }
        return false;
    };
    const auto firstLineBounds = [&](const mdviewer::BlockLayout& item) {
        const mdviewer::LineLayout* line = nullptr;
        const auto findLine = [&](const auto& self, const mdviewer::BlockLayout& block) -> void {
            if (line != nullptr) {
                return;
            }
            if (!block.lines.empty()) {
                line = &block.lines.front();
                return;
            }
            for (const auto& child : block.children) {
                self(self, child);
            }
        };
        findLine(findLine, item);
        Require(line != nullptr, "directional list item should expose a descendant line");
        return SkRect::MakeLTRB(
            item.bounds.left(),
            line->y,
            item.bounds.right(),
            line->y + line->height);
    };
    const SkRect rtlOrderedLine = firstLineBounds(*listItems[3]);
    Require(
        hasNonBackgroundPixel(
            static_cast<int>(std::floor(listItems[3]->bounds.right() + 1.0f)),
            static_cast<int>(std::floor(rtlOrderedLine.top())),
            static_cast<int>(std::ceil(listItems[3]->bounds.right() + 38.0f)),
            static_cast<int>(std::ceil(rtlOrderedLine.bottom()))),
        "RTL ordered markers should remain wholly in the right gutter");
    Require(
        !hasNonBackgroundPixel(
            static_cast<int>(std::floor(listItems[3]->bounds.left() - 38.0f)),
            static_cast<int>(std::floor(rtlOrderedLine.top())),
            static_cast<int>(std::ceil(listItems[3]->bounds.left() - 1.0f)),
            static_cast<int>(std::ceil(rtlOrderedLine.bottom()))),
        "RTL ordered markers should not be drawn in the left gutter");

    const float rtlTaskCenterY = markerCenterY(*listItems[5]);
    Require(
        hasNonBackgroundPixel(
            static_cast<int>(std::floor(listItems[5]->bounds.right() + 8.0f)),
            static_cast<int>(std::floor(rtlTaskCenterY - 9.0f)),
            static_cast<int>(std::ceil(listItems[5]->bounds.right() + 24.0f)),
            static_cast<int>(std::ceil(rtlTaskCenterY + 9.0f))),
        "RTL task checkboxes should render in the mirrored right gutter");
    Require(
        !hasNonBackgroundPixel(
            static_cast<int>(std::floor(listItems[5]->bounds.left() - 24.0f)),
            static_cast<int>(std::floor(rtlTaskCenterY - 9.0f)),
            static_cast<int>(std::ceil(listItems[5]->bounds.left() - 8.0f)),
            static_cast<int>(std::ceil(rtlTaskCenterY + 9.0f))),
        "RTL task checkboxes should not be drawn in the left gutter");

    const int ltrAccentX = static_cast<int>(std::round(quotes[0]->bounds.left() + 1.0f));
    const int rtlAccentX = static_cast<int>(std::round(quotes[1]->bounds.right() - 2.0f));
    RequireEqual(
        pixels.getColor(ltrAccentX, static_cast<int>(std::round(quotes[0]->bounds.centerY()))),
        palette.blockquoteAccent,
        "LTR blockquotes should retain their left accent");
    RequireEqual(
        pixels.getColor(rtlAccentX, static_cast<int>(std::round(quotes[1]->bounds.centerY()))),
        palette.blockquoteAccent,
        "RTL blockquotes should mirror their accent to the right");
    RequireEqual(
        pixels.getColor(
            static_cast<int>(std::round(quotes[1]->bounds.left() + 1.0f)),
            static_cast<int>(std::round(quotes[1]->bounds.centerY()))),
        palette.windowBackground,
        "RTL blockquotes should not retain a stale left accent");

    bool foundRtlAlertHeaderInk = false;
    const int alertTop = static_cast<int>(std::floor(quotes[2]->bounds.top()));
    const int alertRight = static_cast<int>(std::ceil(quotes[2]->bounds.right()));
    for (int y = alertTop; y < std::min(alertTop + 20, pixels.height()); ++y) {
        for (int x = std::max(0, alertRight - 120); x < std::min(alertRight - 6, pixels.width()); ++x) {
            foundRtlAlertHeaderInk = foundRtlAlertHeaderInk ||
                pixels.getColor(x, y) != palette.windowBackground;
        }
    }
    Require(foundRtlAlertHeaderInk,
            "RTL alert icons and titles should be placed from the right edge");

    const mdviewer::DocumentModel detailsDocument = mdviewer::MarkdownParser::Parse(
        "<details>\n"
        "<summary>English summary</summary>\n\n"
        "Body.\n\n"
        "</details>\n\n"
        "<details>\n"
        "<summary>ملخص عربي</summary>\n\n"
        "محتوى.\n\n"
        "</details>\n");
    const auto detailsLayout = mdviewer::LayoutEngine::ComputeLayout(
        detailsDocument,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    RequireEqual(detailsLayout.blocks.size(), static_cast<size_t>(2),
                 "directional details fixture should retain both cards");
    const auto& ltrDetails = detailsLayout.blocks[0];
    const auto& rtlDetails = detailsLayout.blocks[1];
    RequireNear(ltrDetails.lines.front().x, ltrDetails.bounds.left() + 28.0f, 0.01f,
                "LTR details summaries should reserve their left chevron gutter");
    RequireNear(
        rtlDetails.lines.front().x + rtlDetails.lines.front().width,
        rtlDetails.bounds.right() - 28.0f,
        0.01f,
        "RTL details summaries should reserve a mirrored right chevron gutter");

    const mdviewer::DocumentModel tableDocument = mdviewer::MarkdownParser::Parse(
        "| العربية | English | עברית |\n"
        "| :--- | ---: | --- |\n"
        "| قيمة | value | ערך |\n");
    const auto tableLayout = mdviewer::LayoutEngine::ComputeLayout(
        tableDocument,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    const auto& table = FirstBlockOfType(tableLayout, mdviewer::BlockType::Table);
    const auto& firstRow = table.children.front();
    const auto& explicitLeftCell = firstRow.children[0];
    const auto& explicitRightCell = firstRow.children[1];
    const auto& defaultRtlCell = firstRow.children[2];
    RequireNear(explicitLeftCell.lines.front().x, explicitLeftCell.bounds.left() + 12.0f, 0.01f,
                "explicit Markdown left alignment should override an RTL table cell");
    RequireNear(
        explicitRightCell.lines.front().x + explicitRightCell.lines.front().width,
        explicitRightCell.bounds.right() - 12.0f,
        0.01f,
        "explicit Markdown right alignment should remain authoritative in table cells");
    RequireNear(
        defaultRtlCell.lines.front().x + defaultRtlCell.lines.front().width,
        defaultRtlCell.bounds.right() - 12.0f,
        0.01f,
        "default table alignment should independently follow an RTL cell's direction");

    const mdviewer::DocumentModel rtlCodeDocument = mdviewer::MarkdownParser::Parse(
        "```text\n"
        "هذا سطر عربي قصير\n"
        "```\n");
    const auto rtlCodeLayout = mdviewer::LayoutEngine::ComputeLayout(
        rtlCodeDocument,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize);
    const auto& rtlCode = FirstBlockOfType(rtlCodeLayout, mdviewer::BlockType::CodeBlock);
    Require(rtlCode.direction == mdviewer::ResolvedTextDirection::RightToLeft,
            "an Arabic-first code block should retain its RTL block direction");
    Require(rtlCode.lines.front().y - rtlCode.bounds.top() >= 27.5f,
            "RTL code should reserve the top-right control row instead of rendering beneath it");
}

void SoftBreakWordWrappingRegression() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "soft-break wrapping regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());

    const std::string source =
        "gem16 is a local inference stack built specifically for Gemma 4 12B on Blackwell GPUs with about 16 GB of VRAM.\n"
        "The desktop app is the primary entry point: it downloads the pinned model set into the shared Hugging Face cache,\n"
        "starts or attaches to `gem16-server`, and provides a compact multimodal chat UI.\n\n"
        "> [!IMPORTANT]\n"
        "> gem16 is a development preview, not a release-qualified general-purpose runtime. The optimized CUDA backend\n"
        "> currently targets Blackwell SM120/SM120a, and the supported model revisions are pinned deliberately.\n";
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(source);

    const auto isAsciiWordByte = [](char value) {
        const unsigned char byte = static_cast<unsigned char>(value);
        return (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') ||
            byte == '_';
    };
    for (const float width : {520.0f, 760.0f, 1040.0f}) {
        const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
            document,
            width,
            typefaces,
            22.0f);
        const auto verifyBlocks = [&](const auto& self, const auto& blocks) -> void {
            for (const auto& block : blocks) {
                for (const auto& line : block.lines) {
                    Require(line.width <= block.bounds.width() + 0.5f,
                            "shaped prose should not exceed its block's horizontal bounds");
                    for (const auto& run : line.runs) {
                        Require(run.visualX >= -0.5f &&
                                    run.visualX + run.visualWidth <= line.width + 0.5f,
                                "every shaped run should remain inside its recorded line width");
                    }

                    const size_t boundary = line.textStart + line.textLength;
                    if (boundary > 0 && boundary < layout.plainText.size()) {
                        if (isAsciiWordByte(layout.plainText[boundary - 1]) &&
                            isAsciiWordByte(layout.plainText[boundary])) {
                            const size_t contextStart = boundary > 16 ? boundary - 16 : 0;
                            const size_t contextLength = std::min<size_t>(
                                32,
                                layout.plainText.size() - contextStart);
                            throw TestFailure(
                                "soft Markdown breaks must not split an ASCII word near '" +
                                layout.plainText.substr(contextStart, contextLength) + "'");
                        }
                    }
                }
                self(self, block.children);
            }
        };
        verifyBlocks(verifyBlocks, layout.blocks);
    }
}

void ShapedDocumentRendererMigration() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager);
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(fontManager != nullptr && regular != nullptr && bold != nullptr,
            "shaped renderer regression requires document fonts");
    const auto typefaces = MakeTestTypefaces(fontManager.get(), regular.get(), bold.get());
    const std::string source =
        "English [العربية link](https://example.com) and "
        "~~עברית~~ with `code العربية`.\n";
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(source);
    const mdviewer::DocumentLayout originalLayout = mdviewer::LayoutEngine::ComputeLayout(
        document, 760.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    Require(!originalLayout.blocks.empty() && !originalLayout.blocks.front().lines.empty(),
            "shaped renderer fixture should produce a visible line");

    bool foundArabicGlyphs = false;
    for (const auto& line : originalLayout.blocks.front().lines) {
        for (const auto& run : line.runs) {
            foundArabicGlyphs = foundArabicGlyphs ||
                (run.shaped && (run.bidiLevel & 1U) != 0 && !run.glyphs.empty());
        }
    }
    Require(foundArabicGlyphs,
            "renderer fixture should reach an odd-level shaped Arabic glyph run");

    mdviewer::DocumentLayout supportingTextChangedLayout = originalLayout;
    bool replacedShapedText = false;
    const auto replaceShapedSupportingText = [&](const auto& self, auto& blocks) -> void {
        for (auto& block : blocks) {
            for (auto& line : block.lines) {
                for (auto& run : line.runs) {
                    if (run.shaped && !run.text.empty()) {
                        run.text.assign(run.text.size(), ' ');
                        replacedShapedText = true;
                    }
                }
            }
            self(self, block.children);
        }
    };
    replaceShapedSupportingText(
        replaceShapedSupportingText,
        supportingTextChangedLayout.blocks);
    Require(replacedShapedText,
            "renderer fixture should replace shaped supporting UTF-8 without changing glyph data");

    const size_t arabicStart = originalLayout.plainText.find("العربية");
    Require(arabicStart != std::string::npos,
            "renderer fixture should retain Arabic in logical plain text");
    const size_t arabicEnd = arabicStart + std::string("العربية").size();
    const mdviewer::ThemePalette palette = mdviewer::GetThemePalette(mdviewer::ThemeMode::Dark);
    const int renderHeight = static_cast<int>(std::ceil(originalLayout.totalHeight + 24.0f));
    const auto renderLayout = [&](mdviewer::DocumentLayout layout, bool showRanges) {
        mdviewer::AppState state;
        state.sourceText = source;
        state.docLayout = std::move(layout);
        state.outlineCollapsed = true;
        if (showRanges) {
            state.selectionAnchor = arabicStart;
            state.selectionFocus = arabicEnd;
            state.searchActive = true;
            state.searchQuery = "العربية";
            state.searchMatches = {{arabicStart, arabicEnd}};
        }

        const sk_sp<SkSurface> surface = SkSurfaces::Raster(
            SkImageInfo::MakeN32Premul(760, renderHeight));
        Require(surface != nullptr, "shaped renderer regression should create a raster surface");
        surface->getCanvas()->clear(palette.windowBackground);
        mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
            .canvas = surface->getCanvas(),
            .appState = &state,
            .palette = palette,
            .typefaces = typefaces,
            .baseFontSize = mdviewer::kDefaultBaseFontSize,
            .viewportHeight = static_cast<float>(renderHeight),
            .surfaceWidth = 760.0f,
            .surfaceHeight = static_cast<float>(renderHeight),
            .visibleDocumentBottom = state.docLayout.totalHeight,
            .showInteractiveElements = false,
        });
        return surface->makeImageSnapshot();
    };

    const auto requireSamePixels = [](const sk_sp<SkImage>& left, const sk_sp<SkImage>& right, const std::string& message) {
        SkPixmap leftPixels;
        SkPixmap rightPixels;
        Require(left != nullptr && right != nullptr &&
                    left->peekPixels(&leftPixels) && right->peekPixels(&rightPixels),
                "shaped renderer comparison images should expose raster pixels");
        RequireEqual(leftPixels.width(), rightPixels.width(), message + " width");
        RequireEqual(leftPixels.height(), rightPixels.height(), message + " height");
        for (int y = 0; y < leftPixels.height(); ++y) {
            const size_t comparedBytes = static_cast<size_t>(leftPixels.width()) * 4;
            if (std::memcmp(leftPixels.addr(0, y), rightPixels.addr(0, y), comparedBytes) != 0) {
                throw TestFailure(message);
            }
        }
    };

    const sk_sp<SkImage> originalImage = renderLayout(originalLayout, false);
    const sk_sp<SkImage> supportingTextChangedImage = renderLayout(
        supportingTextChangedLayout, false);
    requireSamePixels(
        originalImage,
        supportingTextChangedImage,
        "document text should render from stored glyph IDs and positions, not be reshaped per code point");

    const sk_sp<SkImage> originalRangeImage = renderLayout(originalLayout, true);
    const sk_sp<SkImage> supportingTextChangedRangeImage = renderLayout(
        supportingTextChangedLayout, true);
    requireSamePixels(
        originalRangeImage,
        supportingTextChangedRangeImage,
        "selection, search, and inline decorations should use layout-owned visual geometry");

    const std::string objectSource = "before ![fixture](image.png) after\n";
    const mdviewer::DocumentModel objectDocument = mdviewer::MarkdownParser::Parse(objectSource);
    const mdviewer::DocumentLayout objectLayout = mdviewer::LayoutEngine::ComputeLayout(
        objectDocument,
        760.0f,
        typefaces,
        mdviewer::kDefaultBaseFontSize,
        [](const std::string&) { return std::pair<float, float>{40.0f, 20.0f}; });
    const auto& objectLine = objectLayout.blocks.front().lines.front();
    const auto imageRun = std::find_if(
        objectLine.runs.begin(), objectLine.runs.end(), [](const auto& run) {
            return run.kind == mdviewer::InlineKind::Image;
        });
    Require(imageRun != objectLine.runs.end(),
            "atomic renderer fixture should retain its image placeholder run");

    const sk_sp<SkSurface> redSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(40, 20));
    Require(redSurface != nullptr, "atomic renderer fixture should create an image surface");
    redSurface->getCanvas()->clear(SK_ColorRED);
    const sk_sp<SkImage> redImage = redSurface->makeImageSnapshot();
    mdviewer::AppState objectState;
    objectState.sourceText = objectSource;
    objectState.docLayout = objectLayout;
    objectState.outlineCollapsed = true;
    const sk_sp<SkSurface> objectSurface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(760, static_cast<int>(std::ceil(objectLayout.totalHeight + 24.0f))));
    Require(objectSurface != nullptr, "atomic renderer regression should create a raster surface");
    objectSurface->getCanvas()->clear(palette.windowBackground);
    mdviewer::RenderDocumentScene(mdviewer::DocumentSceneParams{
        .canvas = objectSurface->getCanvas(),
        .appState = &objectState,
        .palette = palette,
        .typefaces = typefaces,
        .baseFontSize = mdviewer::kDefaultBaseFontSize,
        .viewportHeight = static_cast<float>(objectSurface->height()),
        .surfaceWidth = 760.0f,
        .surfaceHeight = static_cast<float>(objectSurface->height()),
        .visibleDocumentBottom = objectLayout.totalHeight,
        .showInteractiveElements = false,
        .resolveImage = [redImage](const std::string&, float, float) { return redImage; },
    });
    SkPixmap objectPixels;
    Require(objectSurface->peekPixels(&objectPixels),
            "atomic renderer surface should expose pixels");
    const int imageCenterX = static_cast<int>(std::round(
        objectLine.x + imageRun->visualX + (imageRun->visualWidth * 0.5f)));
    const int imageCenterY = static_cast<int>(std::round(
        objectLine.y + (objectLine.height * 0.5f)));
    RequireEqual(
        objectPixels.getColor(imageCenterX, imageCenterY),
        static_cast<SkColor>(SK_ColorRED),
        "atomic images should render at their shaped placeholder visual position");
}

void DocumentFontContextFeedsLayout() {
    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> regular = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Normal());
    const sk_sp<SkTypeface> bold = mdviewer::CreateDefaultTypeface(fontManager, SkFontStyle::Bold());
    Require(regular != nullptr && bold != nullptr, "font-context regression requires regular and bold faces");

    const auto typefaces = MakeTestTypefaces(
        fontManager.get(), regular.get(), bold.get(), bold.get(), bold.get());
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(
        "# Shared heading face\n\n"
        "**Shared bold face**\n\n"
        "```text\n"
        "MMMMMMMMiiiiiiii\n"
        "```\n");
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        document, 900.0f, typefaces, mdviewer::kDefaultBaseFontSize);
    const mdviewer::BlockLayout& codeBlock = FirstBlockOfType(layout, mdviewer::BlockType::CodeBlock);
    Require(!codeBlock.lines.empty(), "font-context fixture should produce a code line");

    float expectedCodeWidth = 0.0f;
    for (const auto& line : codeBlock.lines) {
        float lineWidth = 0.0f;
        for (const auto& run : line.runs) {
            SkFont font;
            mdviewer::ConfigureDocumentFont(
                font,
                typefaces,
                mdviewer::BlockType::CodeBlock,
                run.formatting,
                mdviewer::kDefaultBaseFontSize);
            lineWidth += font.measureText(run.text.data(), run.text.size(), SkTextEncoding::kUTF8);
        }
        expectedCodeWidth = std::max(expectedCodeWidth, lineWidth);
    }
    RequireNear(
        codeBlock.codeContentWidth,
        expectedCodeWidth,
        0.01f,
        "layout and rendering should resolve the same code typeface and font style");
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
    const auto typefaces = MakeTestTypefaces(fontMgr.get(), typeface.get());

    const mdviewer::DocumentModel inlineSemantics = mdviewer::MarkdownParser::Parse(
        "Press <kbd>Ctrl</kbd>+<kbd>K</kbd>; H<sub>2</sub>O and x<sup>2</sup>.\n\n"
        "<p>Block HTML also supports <kbd>Enter</kbd>, H<sub>2</sub>O, and x<sup>2</sup>.</p>\n");
    RequireEqual(inlineSemantics.blocks.size(), static_cast<size_t>(2),
                 "safe inline HTML semantics should work in Markdown and allowlisted HTML blocks");
    const auto hasFormatting = [](const mdviewer::Block& block, mdviewer::InlineFormatting formatting) {
        return std::any_of(block.inlineRuns.begin(), block.inlineRuns.end(), [&](const auto& run) {
            return mdviewer::HasFormatting(run.formatting, formatting);
        });
    };
    for (const auto& block : inlineSemantics.blocks) {
        Require(hasFormatting(block, mdviewer::InlineFormatting::Keyboard), "kbd should become native keyboard formatting");
        Require(hasFormatting(block, mdviewer::InlineFormatting::Subscript), "sub should become native subscript formatting");
        Require(hasFormatting(block, mdviewer::InlineFormatting::Superscript), "sup should become native superscript formatting");
    }
    const mdviewer::DocumentModel unsafeInline = mdviewer::MarkdownParser::Parse(
        "Unsafe <kbd onclick=\"run()\">key</kbd> remains source.\n");
    Require(MergeInlineRunText(unsafeInline.blocks[0].inlineRuns).find("onclick") != std::string::npos,
            "attributes on safe inline tags should keep the unsupported source visible");

    mdviewer::DocumentModel details = mdviewer::MarkdownParser::Parse(
        "<details>\n"
        "<summary>Advanced details</summary>\n\n"
        "Hidden **content**.\n\n"
        "</details>\n\n"
        "<details open>\n"
        "<summary>Visible section</summary>\n\n"
        "Visible body.\n\n"
        "</details>\n");
    RequireEqual(details.blocks.size(), static_cast<size_t>(2), "details containers should become native blocks");
    Require(details.blocks[0].type == mdviewer::BlockType::Details && !details.blocks[0].detailsOpen,
            "details without open should start collapsed");
    Require(details.blocks[1].type == mdviewer::BlockType::Details && details.blocks[1].detailsOpen,
            "the safe open attribute should expand details by default");
    Require(details.blocks[0].detailsId != 0 && details.blocks[0].detailsId != details.blocks[1].detailsId,
            "details blocks should receive stable per-document interaction IDs");
    Require(!details.blocks[0].children.empty() && !details.blocks[1].children.empty(),
            "Markdown inside details should remain native child blocks");

    const auto closedDetailsLayout = mdviewer::LayoutEngine::ComputeLayout(
        details, 700.0f, typefaces, 17.0f);
    Require(closedDetailsLayout.plainText.find("Hidden content") == std::string::npos,
            "collapsed details content should not participate in search and copy text");
    Require(closedDetailsLayout.plainText.find("Visible body") != std::string::npos,
            "open details content should participate in search and copy text");
    Require(closedDetailsLayout.blocks[0].children.empty(),
            "collapsed details should omit child layout");
    Require(!closedDetailsLayout.blocks[1].children.empty(),
            "open details should retain child layout");

    const size_t collapsedId = details.blocks[0].detailsId;
    Require(mdviewer::ToggleDetailsBlock(details, collapsedId), "details should toggle through the shared interaction helper");
    const auto openedDetailsLayout = mdviewer::LayoutEngine::ComputeLayout(
        details, 700.0f, typefaces, 17.0f);
    Require(openedDetailsLayout.plainText.find("Hidden content") != std::string::npos,
            "opening details should expose its native child content");
    const auto& detailsLayoutBlock = openedDetailsLayout.blocks[0];
    const float detailsSummaryBottom = detailsLayoutBlock.lines.back().y +
        detailsLayoutBlock.lines.back().height + 5.0f;
    Require(detailsLayoutBlock.children.front().bounds.top() - detailsSummaryBottom >= 12.0f,
            "open details should leave clear padding between summary and body content");
    Require(detailsLayoutBlock.bounds.bottom() - detailsLayoutBlock.children.back().bounds.bottom() >= 15.0f,
            "open details card should include bottom padding below its body content");
    const auto detailsHit = mdviewer::HitTestDocument(
        openedDetailsLayout,
        0.0f,
        30.0f,
        detailsLayoutBlock.bounds.left() + 8.0f,
        detailsLayoutBlock.bounds.top() + 35.0f,
        {});
    RequireEqual(detailsHit.detailsToggleId, collapsedId,
                 "clicking a details summary should expose its toggle ID through shared hit testing");

    const auto layout = mdviewer::LayoutEngine::ComputeLayout(
        document,
        900.0f,
        typefaces,
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
    const auto typefaces = MakeTestTypefaces(fontMgr.get(), typeface.get());
    const auto layout = mdviewer::LayoutEngine::ComputeLayout(document, 680.0f, typefaces, 17.0f);
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
        "[![diagram](diagram.png)](https://example.com/full-diagram)\n");

    const auto imageProvider = [](const std::string& url) {
        return url == "diagram.png" ? std::pair<float, float>{640.0f, 320.0f} : std::pair<float, float>{0.0f, 0.0f};
    };

    const sk_sp<SkFontMgr> fontMgr = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontMgr);
    SkTypeface* typefacePtr = typeface.get();
    const auto typefaces = MakeTestTypefaces(fontMgr.get(), typefacePtr);
    const auto normal = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, typefaces, 17.0f, imageProvider);
    const auto zoomed = mdviewer::LayoutEngine::ComputeLayout(doc, 900.0f, typefaces, 24.0f, imageProvider);
    const auto narrow = mdviewer::LayoutEngine::ComputeLayout(doc, 480.0f, typefaces, 17.0f, imageProvider);

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
    RequireEqual(normalTable.tableTsv, std::string("A\tB\r\nleft\tright"), "tables should expose TSV clipboard text");
    RequireEqual(normalTable.tableCsv, std::string("A,B\r\nleft,right"), "tables should expose CSV clipboard text");
    Require(narrowTable.bounds.width() < normalTable.bounds.width(), "table width should relayout with viewport width");

    const auto tableHit = mdviewer::HitTestDocument(
        normal,
        0.0f,
        30.0f,
        normalTable.bounds.left() + 8.0f,
        normalTable.bounds.top() + 38.0f,
        {});
    RequireEqual(tableHit.tableTsv, normalTable.tableTsv, "hit testing inside a table should expose TSV data");
    RequireEqual(tableHit.tableCsv, normalTable.tableCsv, "hit testing inside a table should expose CSV data");

    const auto& imageRun = imageBlock.lines[0].runs[0];
    const float imageX = imageRun.imageWidth > imageBlock.bounds.width() * 0.8f
        ? imageBlock.bounds.left() + (imageBlock.bounds.width() - imageRun.imageWidth) * 0.5f
        : imageBlock.lines[0].x;
    const auto imageHit = mdviewer::HitTestDocument(
        normal,
        0.0f,
        30.0f,
        imageX + (imageRun.imageWidth * 0.5f),
        imageBlock.lines[0].y + 30.0f + (imageBlock.lines[0].height * 0.5f),
        {});
    Require(imageHit.kind == mdviewer::InlineKind::Image, "hit testing an image should retain image semantics");
    RequireEqual(imageHit.imageSource, std::string("diagram.png"), "image hit testing should expose the image source separately");
    RequireEqual(imageHit.url, std::string("https://example.com/full-diagram"), "a linked image should expose its link inside the image bounds");

    const auto besideImageHit = mdviewer::HitTestDocument(
        normal,
        0.0f,
        30.0f,
        imageX + imageRun.imageWidth + 20.0f,
        imageBlock.lines[0].y + 30.0f + (imageBlock.lines[0].height * 0.5f),
        {});
    Require(besideImageHit.kind == mdviewer::InlineKind::Text, "free space beside an image should not retain image semantics");
    Require(besideImageHit.url.empty(), "free space beside a linked image should not activate its link");
    Require(besideImageHit.linkTarget.empty(), "free space beside a linked image should not expose its link target");
    Require(besideImageHit.imageSource.empty(), "free space beside an image should not expose its source");

    const auto csvLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "| Name | Note |\n"
            "| - | - |\n"
            "| Alpha | hello, \"world\" |\n"),
        700.0f,
        typefaces,
        17.0f);
    const auto& csvTable = FirstBlockOfType(csvLayout, mdviewer::BlockType::Table);
    RequireEqual(
        csvTable.tableCsv,
        std::string("Name,Note\r\nAlpha,\"hello, \"\"world\"\"\""),
        "CSV table copy should quote commas and embedded quotes");

    const auto wideInsets = mdviewer::GetDocumentHorizontalInsets(900.0f);
    const auto compactInsets = mdviewer::GetDocumentHorizontalInsets(480.0f);
    RequireNear(wideInsets.left, 40.0f, 0.001f, "wide documents should retain the established horizontal margin");
    RequireNear(wideInsets.right, wideInsets.left, 0.001f, "wide documents should use symmetric horizontal margins");
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
        typefaces,
        17.0f);
    const auto& wideTable = FirstBlockOfType(wideTableLayout, mdviewer::BlockType::Table);
    Require(wideTable.horizontalContentWidth > wideTable.horizontalViewportWidth,
            "wide tables should retain natural columns and expose horizontal overflow");
    Require(!wideTable.children.empty() && wideTable.children[0].bounds.width() > wideTable.bounds.width(),
            "table rows should keep their content width behind the clipped viewport");
    RequireNear(wideTable.fontScale, 1.0f, 0.001f, "interactive tables should never shrink their font to fit");

    const auto printTableLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "| Engine | Checkpoint / KV | Prefill tok/s | TTFT | Effective D2H | ITL | Sampled peak VRAM | Notes |\n"
            "| - | - | - | - | - | - | - | - |\n"
            "| gem16 | direct-FP8-NVFP4-checkpoint | 5866.86 | 2792.64 ms | 87.66 | 11.408 ms | 11746 MiB | reproducible-result |\n"),
        420.0f,
        typefaces,
        17.0f,
        nullptr,
        mdviewer::LayoutOptions{
            .fitHorizontalOverflow = true,
            .reserveHorizontalScrollbarSpace = false,
            .minimumHorizontalFitScale = 0.72f,
        });
    const auto& printTable = FirstBlockOfType(printTableLayout, mdviewer::BlockType::Table);
    Require(printTable.fontScale < 1.0f && printTable.fontScale >= 0.72f,
            "print tables should shrink only within the configured readability floor");
    Require(printTable.horizontalContentWidth < wideTable.horizontalContentWidth,
            "print table fitting should reduce horizontal content width");

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
        typefaces,
        17.0f);
    Require(!longTokenLayout.blocks.empty() && longTokenLayout.blocks[0].lines.size() > 1,
            "very long unbroken tokens should wrap at UTF-8 boundaries");

    const auto overflowingCodeLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "```cpp\n"
            "const std::string message = \"This deliberately long C++ source line must remain on one visual code line\";\n"
            "```\n"),
        320.0f,
        typefaces,
        17.0f);
    const auto& overflowingCode = FirstBlockOfType(overflowingCodeLayout, mdviewer::BlockType::CodeBlock);
    RequireEqual(overflowingCode.lines.size(), static_cast<size_t>(1), "overflowing code should preserve source lines instead of wrapping");
    Require(
        overflowingCode.codeContentWidth > overflowingCode.codeViewportWidth,
        "overflowing code should record a horizontal scroll range");
    RequireNear(overflowingCode.fontScale, 1.0f, 0.001f, "interactive code should never shrink its font to fit");

    const auto printCodeLayout = mdviewer::LayoutEngine::ComputeLayout(
        mdviewer::MarkdownParser::Parse(
            "```cpp\n"
            "const std::string message = \"This deliberately long C++ source line must remain on one visual code line\";\n"
            "```\n"),
        320.0f,
        typefaces,
        17.0f,
        nullptr,
        mdviewer::LayoutOptions{
            .fitHorizontalOverflow = true,
            .reserveHorizontalScrollbarSpace = false,
            .minimumHorizontalFitScale = 0.72f,
        });
    const auto& printCode = FirstBlockOfType(printCodeLayout, mdviewer::BlockType::CodeBlock);
    Require(printCode.fontScale < 1.0f && printCode.fontScale >= 0.72f,
            "print code should shrink only within the configured readability floor");
    Require(printCode.codeContentWidth < overflowingCode.codeContentWidth,
            "print code fitting should reduce horizontal content width");

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
    const fs::path sourcePath = SourceRoot() / "test-docs" / "bidi-complex-text.md";
    const fs::path outputPath = temp.Path() / "export-output.pdf";
    const std::string source = ReadText(sourcePath);

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

    mdviewer::PreparedPrintDocument prepared;
    Require(
        mdviewer::PreparePrintDocument(
            mdviewer::PrintDocumentRequest{
                .sourcePath = sourcePath,
                .sourceText = source,
                .document = doc,
                .theme = mdviewer::ThemeMode::Light,
                .baseFontSize = mdviewer::kDefaultBaseFontSize,
                .typefaces = typefaces,
                .pageWidth = request.pageWidth,
                .pageHeight = 420.0f,
                .marginTop = 32.0f,
                .marginBottom = 36.0f,
            },
            prepared),
        "print preparation should accept shaped mixed-direction text");
    Require(prepared.pages.size() >= 2,
            "the complex-text fixture should produce multiple valid print pages");
    RequireNear(prepared.pages.front().top, 0.0f, 0.01f,
                "print page ranges should begin at the top of the document");
    for (size_t index = 0; index < prepared.pages.size(); ++index) {
        const mdviewer::PrintPageRange& page = prepared.pages[index];
        Require(page.bottom > page.top,
                "every complex-text print page should have a positive source range");
        if (index > 0) {
            RequireNear(page.top, prepared.pages[index - 1].bottom, 0.01f,
                        "complex-text print page ranges should be contiguous");
        }

        const sk_sp<SkSurface> pageSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(
            static_cast<int>(std::ceil(prepared.pageWidth)),
            static_cast<int>(std::ceil(prepared.pageHeight))));
        Require(pageSurface != nullptr,
                "complex-text print regression should create a raster page surface");
        mdviewer::RenderPrintDocumentPage(prepared, index, pageSurface->getCanvas(), {});
        SkPixmap pagePixels;
        Require(pageSurface->peekPixels(&pagePixels),
                "rendered complex-text print pages should expose raster pixels");
        const SkColor background = mdviewer::GetThemePalette(mdviewer::ThemeMode::Light).windowBackground;
        bool foundPageInk = false;
        for (int y = 0; y < pagePixels.height() && !foundPageInk; ++y) {
            for (int x = 0; x < pagePixels.width(); ++x) {
                if (pagePixels.getColor(x, y) != background) {
                    foundPageInk = true;
                    break;
                }
            }
        }
        Require(foundPageInk,
                "every complex-text print page should render visible document content");
    }
    RequireNear(
        prepared.pages.back().bottom,
        prepared.appState.docLayout.totalHeight,
        0.01f,
        "print page ranges should cover the complete complex-text layout");

    const mdviewer::BlockLayout* printedRtlBlock = nullptr;
    const auto findPrintedRtlBlock = [&](const auto& self, const auto& blocks) -> void {
        for (const auto& block : blocks) {
            if (printedRtlBlock == nullptr &&
                block.direction == mdviewer::ResolvedTextDirection::RightToLeft &&
                !block.lines.empty()) {
                printedRtlBlock = &block;
                return;
            }
            self(self, block.children);
        }
    };
    findPrintedRtlBlock(findPrintedRtlBlock, prepared.appState.docLayout.blocks);
    Require(
        printedRtlBlock != nullptr,
        "print preparation should use the same RTL shaping layout as the screen");
    RequireNear(
        printedRtlBlock->lines.front().x + printedRtlBlock->lines.front().width,
        printedRtlBlock->bounds.right(),
        0.01f,
        "print RTL geometry should remain right-aligned before PDF rendering");

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
        {"Open", "Ctrl+O", false},
        {"", "", true},
        {"Exit", "", false},
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

void UnicodeSearchCaseFolding() {
    const std::string street = "Stra\xC3\x9F" "e";
    const std::string upperStreet = "STRASSE";
    const std::string upperUber = "\xC3\x9C" "ber";
    const std::string mixedUber = "\xC3\xBC" "BER";

    mdviewer::AppState searchState;
    searchState.docLayout.plainText =
        street + " " + upperStreet + " " + upperUber + " " + mixedUber;
    mdviewer::OpenSearch(searchState);
    mdviewer::InsertSearchText(searchState, upperStreet);
    RequireEqual(searchState.searchMatches.size(), static_cast<size_t>(2),
                 "Unicode case folding should match both sharp-s and ASCII spellings");
    RequireEqual(searchState.searchMatches[0].first, static_cast<size_t>(0),
                 "case-folded search should retain the original match start");
    RequireEqual(searchState.searchMatches[0].second, street.size(),
                 "case-folded search should retain the original sharp-s byte range");

    mdviewer::CloseSearch(searchState);
    mdviewer::OpenSearch(searchState);
    mdviewer::InsertSearchText(searchState, "\xC3\xBC" "ber");
    RequireEqual(searchState.searchMatches.size(), static_cast<size_t>(2),
                 "Unicode case folding should match accented letters independent of case");

    searchState.docLayout.plainText = "\xC3\x9F";
    searchState.searchQuery = "s";
    mdviewer::RebuildSearchMatches(searchState);
    Require(searchState.searchMatches.empty(),
            "search should not match only half of a case-folded source character");
    searchState.searchQuery = "ss";
    mdviewer::RebuildSearchMatches(searchState);
    RequireEqual(searchState.searchMatches.size(), static_cast<size_t>(1),
                 "the full sharp-s case fold should remain searchable");
    RequireEqual(searchState.searchMatches[0].second, static_cast<size_t>(2),
                 "a folded expansion should map back to the complete UTF-8 source character");
}

void LatexMathParsingLayoutAndFallback() {
    const mdviewer::DocumentModel document = mdviewer::MarkdownParser::Parse(
        "Euler: $e^{i\\pi}+1=0$ is inline.\n\n"
        "$$\\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}$$\n\n"
        "Code stays literal: `$not_math$`.\n\n"
        "Linked formula: [$x^2$](https://example.com/math).\n\n"
        "Ordinary currency: $5.00.\n");

    RequireEqual(document.blocks.size(), static_cast<size_t>(5), "math fixture should produce five paragraphs");
    const auto findMathRun = [](const mdviewer::Block& block) -> const mdviewer::InlineRun* {
        const auto found = std::find_if(block.inlineRuns.begin(), block.inlineRuns.end(), [](const auto& run) {
            return run.kind == mdviewer::InlineKind::Math;
        });
        return found == block.inlineRuns.end() ? nullptr : &*found;
    };

    const mdviewer::InlineRun* inlineMath = findMathRun(document.blocks[0]);
    Require(inlineMath != nullptr, "md4c should recognize inline dollar math");
    RequireEqual(inlineMath->mathSource, std::string("e^{i\\pi}+1=0"), "inline math should retain delimiter-free render source");
    RequireEqual(inlineMath->text, std::string("$e^{i\\pi}+1=0$"), "inline math should retain copyable Markdown source");
    Require(!inlineMath->mathDisplay, "single-dollar math should use inline style");

    const mdviewer::InlineRun* displayMath = findMathRun(document.blocks[1]);
    Require(displayMath != nullptr && displayMath->mathDisplay, "double-dollar math should use display style");
    RequireEqual(
        displayMath->mathSource,
        std::string("\\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}"),
        "display math should preserve TeX commands");

    Require(findMathRun(document.blocks[2]) == nullptr, "inline code should suppress math parsing");
    const mdviewer::InlineRun* linkedMath = findMathRun(document.blocks[3]);
    Require(linkedMath != nullptr, "math should remain available inside links");
    RequireEqual(linkedMath->linkTarget, std::string("https://example.com/math"), "math should retain its link target");
    Require(findMathRun(document.blocks[4]) == nullptr, "a normal unmatched currency dollar should remain ordinary text without escaping");

    const mdviewer::DocumentModel ordinaryDollars = mdviewer::MarkdownParser::Parse(
        "One unmatched dollar: $5.00.\n\n"
        "Two ordinary prices: $5.00 and $10.00.\n\n"
        "Shell variables: $HOME and $PATH.\n\n"
        "A dollar-wrapped word stays literal: $sale$.\n\n"
        "An unmatched double marker stays literal: $$.\n\n"
        "Intentional math remains: $x$ and $x + y$.\n");
    RequireEqual(ordinaryDollars.blocks.size(), static_cast<size_t>(6), "dollar regression fixture should produce six paragraphs");
    for (size_t index = 0; index < 5; ++index) {
        Require(
            findMathRun(ordinaryDollars.blocks[index]) == nullptr,
            "ordinary prices, shell variables, words, and unmatched dollars must remain Markdown text");
    }
    RequireEqual(
        std::count_if(
            ordinaryDollars.blocks[5].inlineRuns.begin(),
            ordinaryDollars.blocks[5].inlineRuns.end(),
            [](const auto& run) { return run.kind == mdviewer::InlineKind::Math; }),
        static_cast<std::ptrdiff_t>(2),
        "unambiguous inline formulas should still render as math");

    const mdviewer::DocumentModel matrixDocument = mdviewer::MarkdownParser::Parse(
        "$$\n"
        "A = \\begin{pmatrix}\n"
        "  a & b \\\\ \n"
        "  c & d\n"
        "\\end{pmatrix}\n"
        "$$\n");
    RequireEqual(matrixDocument.blocks.size(), static_cast<size_t>(1), "multiline matrix should remain one paragraph");
    RequireEqual(matrixDocument.blocks[0].inlineRuns.size(), static_cast<size_t>(1), "all md4c callbacks inside one math span should aggregate into one run");
    const auto& matrixRun = matrixDocument.blocks[0].inlineRuns.front();
    Require(matrixRun.kind == mdviewer::InlineKind::Math && matrixRun.mathDisplay, "multiline matrix should remain one display-math run");
    Require(
        matrixRun.mathSource.find("\\begin{pmatrix}") != std::string::npos &&
            matrixRun.mathSource.find("\\end{pmatrix}") != std::string::npos &&
            matrixRun.mathSource.find("a & b \\\\") != std::string::npos,
        "multiline matrix callbacks should preserve the complete TeX environment");

    const sk_sp<SkFontMgr> fontManager = mdviewer::CreateFontManager();
    const sk_sp<SkTypeface> typeface = mdviewer::CreateDefaultTypeface(fontManager);
    const auto typefaces = MakeTestTypefaces(fontManager.get(), typeface.get());
    const mdviewer::DocumentLayout layout = mdviewer::LayoutEngine::ComputeLayout(
        document, 760.0f, typefaces, 17.0f);
    const mdviewer::DocumentLayout matrixLayout = mdviewer::LayoutEngine::ComputeLayout(
        matrixDocument, 760.0f, typefaces, 17.0f);
    Require(
        matrixLayout.blocks.size() == 1 && matrixLayout.blocks[0].lines.size() == 1 &&
            matrixLayout.blocks[0].lines[0].runs.size() == 1 &&
            matrixLayout.blocks[0].lines[0].runs[0].mathLayout.valid,
        "multiline pmatrix should produce one valid native math layout");
    Require(
        layout.plainText.find("$e^{i\\pi}+1=0$") != std::string::npos &&
            layout.plainText.find("$$\\frac") != std::string::npos,
        "copy/search text should preserve both kinds of math delimiters");

    const mdviewer::RunLayout* inlineLayout = nullptr;
    for (const auto& run : layout.blocks[0].lines.front().runs) {
        if (run.kind == mdviewer::InlineKind::Math) inlineLayout = &run;
    }
    Require(inlineLayout != nullptr && inlineLayout->mathLayout.valid, "inline formula should produce native MicroTeX layout");
    Require(inlineLayout->visualWidth > 0.0f, "inline formula should reserve visual width");
    const sk_sp<SkSurface> mathSurface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 120));
    Require(mathSurface != nullptr, "math drawing regression should create a raster surface");
    mathSurface->getCanvas()->clear(SK_ColorTRANSPARENT);
    mdviewer::DrawMath(
        mathSurface->getCanvas(),
        inlineLayout->mathLayout,
        8.0f,
        8.0f,
        SK_ColorBLACK);
    SkPixmap mathPixels;
    Require(mathSurface->peekPixels(&mathPixels), "math drawing pixels should be readable");
    bool hasMathPixel = false;
    for (int y = 0; y < mathPixels.height() && !hasMathPixel; ++y) {
        for (int x = 0; x < mathPixels.width(); ++x) {
            if (SkColorGetA(*mathPixels.addr32(x, y)) != 0) {
                hasMathPixel = true;
                break;
            }
        }
    }
    Require(hasMathPixel, "native MicroTeX drawing should produce visible Skia pixels");

    const auto& displayLine = layout.blocks[1].lines.front();
    RequireEqual(displayLine.runs.size(), static_cast<size_t>(1), "display formula should occupy a dedicated line");
    Require(displayLine.runs.front().mathLayout.valid, "display formula should produce native MicroTeX layout");
    Require(displayLine.x > layout.blocks[1].bounds.left(), "display formula should be centered in its content width");

    const std::vector<std::string> representativeMath = {
        "\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}",
        "\\begin{pmatrix}a & b \\\\ c & d\\end{pmatrix}",
        "\\left\\lVert \\vec{v} \\right\\rVert = \\sqrt{x^2+y^2}",
    };
    for (const auto& source : representativeMath) {
        Require(
            mdviewer::LayoutMath(source, true, 17.0f, 600.0f).valid,
            "representative operators, matrices, accents, and delimiters should be supported");
    }

    const mdviewer::DocumentTextHit rightHalfHit = mdviewer::HitTestDocument(
        layout,
        0.0f,
        0.0f,
        displayLine.x + displayLine.runs.front().visualWidth * 0.75f,
        displayLine.y + displayLine.height * 0.5f,
        {});
    RequireEqual(
        rightHalfHit.position,
        displayLine.runs.front().textStart + displayLine.runs.front().text.size(),
        "math hit testing should treat a formula as one atomic selectable run");

    const mdviewer::MathLayout oversized = mdviewer::LayoutMath(
        std::string(8193, 'x'), false, 17.0f, 600.0f);
    Require(!oversized.valid, "oversized math input should use the safe source fallback");
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
    const auto typefaces = MakeTestTypefaces(fontMgr.get(), typefacePtr);

    const auto normal = mdviewer::LayoutEngine::ComputeLayout(doc, 760.0f, typefaces, 17.0f);
    const auto zoomed = mdviewer::LayoutEngine::ComputeLayout(doc, 760.0f, typefaces, 24.0f);
    const auto wide = mdviewer::LayoutEngine::ComputeLayout(doc, 920.0f, typefaces, 17.0f);
    const auto narrow = mdviewer::LayoutEngine::ComputeLayout(doc, 420.0f, typefaces, 17.0f);
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
        {"ComplexTextOutlineRendering", ComplexTextOutlineRendering},
        {"HitTestingUsesOnlyClosestLine", HitTestingUsesOnlyClosestLine},
        {"Utf8Boundaries", Utf8Boundaries},
        {"MarkdownSafetyLimits", MarkdownSafetyLimits},
        {"DocumentSizeLimit", DocumentSizeLimit},
        {"FrontMatterAndMarkdownExtensions", FrontMatterAndMarkdownExtensions},
        {"MarkdownCorrectnessFoundation", MarkdownCorrectnessFoundation},
        {"UnicodeSearchCaseFolding", UnicodeSearchCaseFolding},
        {"BidirectionalMarkdownBaseline", BidirectionalMarkdownBaseline},
        {"ComplexTextRuntimeAvailability", ComplexTextRuntimeAvailability},
        {"ComplexTextShapingSubsystem", ComplexTextShapingSubsystem},
        {"SharedTextGeometryModel", SharedTextGeometryModel},
        {"LayoutOwnedDocumentHitTesting", LayoutOwnedDocumentHitTesting},
        {"ShapedLayoutEngineIntegration", ShapedLayoutEngineIntegration},
        {"DirectionAwareBlockDecorations", DirectionAwareBlockDecorations},
        {"SoftBreakWordWrappingRegression", SoftBreakWordWrappingRegression},
        {"ShapedDocumentRendererMigration", ShapedDocumentRendererMigration},
        {"DocumentFontContextFeedsLayout", DocumentFontContextFeedsLayout},
        {"SafeHtmlSubset", SafeHtmlSubset},
        {"GithubAlerts", GithubAlerts},
        {"LayoutSensitiveBehavior", LayoutSensitiveBehavior},
        {"PdfExportWritesFile", PdfExportWritesFile},
        {"MenuLayoutHitTesting", MenuLayoutHitTesting},
        {"SyntaxHighlightingCacheAndFallback", SyntaxHighlightingCacheAndFallback},
        {"LatexMathParsingLayoutAndFallback", LatexMathParsingLayoutAndFallback},
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
