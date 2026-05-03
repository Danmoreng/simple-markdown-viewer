# Simple Markdown Viewer Development Plan

## Purpose

This document tracks product and engineering milestones for Simple Markdown Viewer.
It is separate from `AGENTS.md`, which should remain focused on instructions for agents working on the project.

The product remains:

- [ ] single-window
- [ ] read-only
- [ ] native desktop
- [ ] Skia-rendered
- [ ] browser-free
- [ ] focused on Markdown viewing, navigation, search, and safe file/link handling

## Current Baseline

The current implementation already includes:

- [x] Markdown parsing with `md4c`
- [x] GitHub-style tables, task lists, strikethrough, links, images, and fenced code blocks
- [x] Tree-sitter syntax highlighting for supported fenced code languages
- [x] code block copy buttons
- [x] custom Skia document rendering
- [x] scrolling, selection, and copy
- [x] in-document search with match highlighting and next/previous navigation
- [x] relative local Markdown/text links opening inside the viewer
- [x] external web links opening through the platform shell
- [x] link hover preview
- [x] navigation history with back/forward
- [x] recent files
- [x] light, sepia, and dark themes
- [x] runtime document font selection
- [x] reader zoom in/out
- [x] persistent config for theme, font, zoom, and recent files
- [x] Windows and Linux drag-and-drop and native file dialogs
- [x] Windows live reload when the current file changes
- [x] a Linux host in-tree and working on the shared controller/render/view stack
- [x] shared custom top-bar layout, drawing, toolbar hit testing, and dropdown drawing in `src/render/menu_renderer.*`

## Milestone 1: Plan Hygiene and Test Coverage

Goal: make the current behavior safer to change before adding larger user-facing features.

- [x] Keep `AGENTS.md` limited to repository orientation, setup, build commands, and architectural boundaries.
- [x] Keep development milestones, backlog, and product decisions in this document.
- [x] Add focused automated tests for config parsing and saving.
- [x] Add tests for recent files and navigation history behavior.
- [x] Add tests for link resolution, including fragments, percent-encoded paths, spaces, relative paths, extensionless text files, missing files, and unsafe schemes.
- [x] Add tests for heading anchor generation, including duplicate headings and Unicode/emoji fallback expectations.
- [x] Add tests for layout-sensitive behavior around tables, code blocks, images, and zoom relayout.
- [x] Keep platform menu cleanup separate from product feature work unless a feature directly touches command routing.
- [x] Tighten menu/top-bar ownership so shared code owns platform-neutral menu layout, drawing, and hit-test behavior, while hosts own native popup/dropdown integration, command dispatch, and event translation.

## Milestone 1a: Config, Theme, and Zoom Follow-ups

Goal: finish the remaining small persistence and reader-control details without expanding the settings surface.

- [ ] Add a per-user config fallback when `mdviewer.ini` cannot be written next to the executable.
- [ ] Keep config saving explicit and robust, with invalid or missing values falling back to safe defaults.
- [ ] Decide whether theme palette overrides in config remain supported long-term, and document the supported keys if they do.
- [ ] Consider `Ctrl` + mouse wheel zoom if it fits the interaction model.
- [ ] Preserve reading position as well as practical after zoom, relayout, manual reload, and automatic reload.

## Milestone 2: Document Navigation Upgrade

Goal: make long documents substantially easier to navigate.

- [ ] Add a sidebar table of contents generated from headings.
- [ ] Support nested heading hierarchy and skipped heading levels.
- [ ] Highlight the current section while scrolling.
- [ ] Click outline items to jump to headings.
- [ ] Add keyboard navigation for the outline.
- [ ] Add a command and shortcut to show/hide the outline.
- [ ] Preserve back/forward behavior for internal heading jumps.
- [ ] Improve heading anchor compatibility with common GitHub-style expectations, including duplicate headings, Unicode headings, emoji, punctuation, and percent-decoded fragments.
- [ ] Add copy-heading-link support if it fits cleanly into the context menu model.

## Milestone 3: Safer Links and Better File Context

Goal: make link behavior predictable, inspectable, and safer.

- [ ] Add explicit handling for suspicious or unsupported URL schemes such as `javascript:`, custom app protocols, and shell-like targets.
- [ ] Warn before opening executable local files or clearly external local paths.
- [ ] Show clear feedback for broken local links.
- [ ] Add document/background context menu actions for reload, copy document path, and reveal in file manager.
- [ ] Add link context menu actions for reveal target in file manager when the target is local.
- [ ] Add a manual reload command and shortcut.
- [ ] Preserve scroll position after manual or automatic reload when practical.
- [ ] Add remembered scroll position per recent file.

## Milestone 4: Markdown Compatibility Improvements

Goal: improve fidelity for real-world documentation without turning the app into a browser.

- [ ] Add front matter detection for YAML, TOML, and JSON front matter at the start of a file.
- [ ] Decide whether front matter is shown, collapsed, or hidden by default.
- [ ] Add a deliberate raw HTML policy.
- [ ] Support a small safe subset if chosen, such as `<br>`, `<kbd>`, `<sub>`, `<sup>`, `<details>`, and `<summary>`.
- [ ] Suppress or show-as-source unsafe HTML such as scripts, event handlers, iframes, and external embeds.
- [ ] Add footnote rendering and reference/back-reference navigation if the parser support is sufficient.
- [ ] Consider GitHub-style alerts/admonitions such as `[!NOTE]`, `[!TIP]`, and `[!WARNING]`.
- [ ] Expand recognized Markdown-related extensions to include `.mdown`, `.mkd`, and possibly `.mdx` as a partial/fallback mode.

## Milestone 5: Tables, Images, and Copy Fidelity

Goal: polish the most visible rendering and copy edge cases.

- [ ] Add wide-table horizontal scrolling or another clear overflow strategy.
- [ ] Preserve column alignment and wrapping for narrow or long-cell tables.
- [ ] Add copy table as plain text or TSV.
- [ ] Improve behavior for very long unbroken strings and URLs.
- [ ] Add image context menu actions for open image, copy image path, and reveal image in file manager.
- [ ] Add an image zoom or lightbox view if it can stay within the single-window viewer model.
- [ ] Decide remote image policy.
- [ ] If remote images are supported, block them by default or expose a clear setting before loading them.
- [ ] Treat SVG carefully; do not execute active content or external references.

## Milestone 6: Search and Keyboard Usability

Goal: make search and keyboard workflows closer to expected desktop behavior.

- [ ] Add case-sensitive search toggle.
- [ ] Add whole-word search toggle.
- [ ] Consider regex search as optional.
- [ ] Improve Unicode-aware search behavior beyond ASCII lowercase matching.
- [ ] Add select-all support.
- [ ] Add zoom reset command and shortcut.
- [ ] Add keyboard access for links, outline items, and top-bar controls.
- [ ] Add visible focus indication for keyboard navigation.
- [ ] Review shortcut coverage for open, find, find next/previous, copy, reload, zoom reset, back/forward, print, and outline toggle.

## Milestone 7: Themes and Accessibility

Goal: make the custom-rendered UI safer for everyday and accessibility use.

- [ ] Add system theme mode.
- [ ] Add high-contrast theme.
- [ ] Check color contrast for body text, links, code, syntax highlighting, search highlights, selection, blockquotes, tables, and disabled menu items.
- [ ] Avoid conveying link/search/focus state by color alone.
- [ ] Respect platform high-contrast settings where practical.
- [ ] Improve text scaling behavior and verify layout does not break at zoom bounds.
- [ ] Define what accessibility support is feasible for a Skia-rendered document surface, especially headings, links, tables, and focus state.

## Milestone 8: Print and Export

Goal: support common output workflows without compromising the viewer architecture.

- [ ] Add native print support.
- [ ] Add export to PDF.
- [ ] Use a print-friendly light palette by default, independent of the active dark theme.
- [ ] Preserve links in exported PDF if practical.
- [ ] Handle page breaks for headings, paragraphs, code blocks, tables, and images.
- [ ] Avoid clipping wide tables and long code lines.
- [ ] Include optional document title and page numbers if implementation cost is reasonable.

## Milestone 9: Performance and Large Files

Goal: keep the app responsive with large or generated Markdown files.

- [ ] Add file-size and content-complexity thresholds for warnings or degraded mode.
- [ ] Avoid blocking the UI during expensive image preloading, syntax highlighting, search, or layout where practical.
- [ ] Consider incremental or cancellable layout for very large documents.
- [ ] Add graceful feedback for files too large to render comfortably.
- [ ] Cache render artifacts where useful without producing stale output after reload.
- [ ] Add performance fixtures for huge tables, long code blocks, many headings, many links, and many images.

## Milestone 10: Linux Hardening

Goal: harden the second native host and keep it aligned with the shared viewer behavior.

- [ ] Keep Linux build and runtime startup validated.
- [ ] Validate Linux file open dialog, clipboard, shell open, drag/drop if supported, menu commands, font selection, Skia surface creation, DPI behavior, and context menus.
- [ ] Match shared viewer behavior with Windows for opening files, links, search, selection, copy, themes, zoom, recent files, and history.
- [ ] Add Linux-specific packaging notes after runtime validation.
- [ ] Do not introduce platform-only product behavior unless required by native platform conventions.

## Later Backlog

- [ ] Native browser-free Mermaid rendering for fenced `mermaid` code blocks.
  - [ ] Do not use Chromium, Puppeteer, a webview, or the Mermaid CLI rendering pipeline.
  - [ ] Prefer a lightweight native integration, likely via a Rust static library/C ABI bridge using a mature browser-free renderer such as `mermaid-rs-renderer`.
  - [ ] Render into an app-native image surface, preferably PNG bytes or another Skia-friendly raster path, because the current Skia build does not enable SVG rendering.
  - [ ] Cache rendered diagrams by source hash.
  - [ ] Fall back to the original code block plus an error message when rendering fails.
  - [ ] Defer this until the Rust/Cargo build dependency and CI/release impact are acceptable.
- [ ] Optional math rendering with safe fallback and copy-source support.
- [ ] Definition lists.
- [ ] Wiki links.
- [ ] Command palette.
- [ ] Session restore.
- [ ] Optional focus mode.
- [ ] Sticky table headers.
- [ ] More code language grammars if demand justifies the build size and dependency impact.

## Explicit Product Decisions Still Needed

- [ ] Which Markdown dialect is the primary compatibility target: CommonMark, GitHub-flavored Markdown, documentation-site Markdown, or broad tolerant Markdown?
- [ ] Should raw HTML render, be sanitized, or be shown as source?
- [ ] Should front matter be visible, collapsed, or hidden by default?
- [ ] Should remote images ever load automatically?
- [ ] Should local links outside the current document tree require confirmation?
- [ ] Should print/export preserve the active theme or always use a print-friendly theme?
- [ ] What level of accessibility is expected for a custom-rendered document surface?
- [ ] Should diagrams and math become first-class features or stay optional enhancements?
