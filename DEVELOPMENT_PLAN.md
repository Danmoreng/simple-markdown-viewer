# Simple Markdown Viewer Development Plan

## Purpose

This document tracks product and engineering milestones for Simple Markdown Viewer.
It is separate from `AGENTS.md`, which should remain focused on instructions for agents working on the project.
The durable product scope and target experience are documented in
[`docs/PRODUCT_VISION.md`](docs/PRODUCT_VISION.md).

The product remains:

- [x] single-window
- [x] read-only
- [x] native desktop
- [x] Skia-rendered
- [x] browser-free
- [x] focused on Markdown viewing, navigation, search, and safe file/link handling

## Current Baseline

The current repository-specific stability and compatibility findings are tracked
in [`docs/LINUX_STABILITY_AND_MARKDOWN_AUDIT.md`](docs/LINUX_STABILITY_AND_MARKDOWN_AUDIT.md).
Linux crash hardening and resource limits should be completed before broadening
the Markdown feature surface.

The current implementation already includes:

- [x] Markdown parsing with `md4c`
- [x] GitHub-style tables, task lists, strikethrough, links, images, and fenced code blocks
- [x] Tree-sitter syntax highlighting for supported fenced code languages
- [x] code block copy buttons
- [x] custom Skia document rendering
- [x] scrolling, selection, and copy
- [x] Unicode-aware in-document search with match highlighting and next/previous navigation
- [x] relative local Markdown/text links opening inside the viewer
- [x] external web links opening through the platform shell
- [x] link hover preview
- [x] navigation history with back/forward
- [x] recent files
- [x] light, sepia, and dark themes
- [x] runtime document font selection
- [x] reader zoom in/out/reset
- [x] persistent config for theme, font, zoom, recent files, and window placement on Windows and Linux
- [x] Windows and Linux drag-and-drop and native file dialogs
- [x] Windows live reload when the current file changes
- [x] a Linux host in-tree and working on the shared controller/render/view stack
- [x] shared custom top-bar layout, drawing, toolbar hit testing, and dropdown drawing in `src/render/menu_renderer.*`
- [x] native browser-free LaTeX math rendering with conservative dollar-sign handling and source-preserving interaction

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
- [x] Keep Win32 mouse-capture transitions outside the app-state mutex so synchronous `WM_CAPTURECHANGED` re-entry cannot terminate the app.

## Milestone 1a: Config, Theme, and Zoom Follow-ups

Goal: finish the remaining small persistence and reader-control details without expanding the settings surface.

- [x] Use a canonical per-user `mdviewer.ini` path, with one-time-compatible loading from legacy executable-adjacent config when the per-user file does not exist.
- [x] Keep config saving explicit and robust, with invalid or missing values falling back to safe defaults.
- [x] Decide whether theme palette overrides in config remain supported long-term: palette overrides are not supported; config supports named `theme` values only.
- [x] Consider `Ctrl` + mouse wheel zoom if it fits the interaction model.
- [x] Preserve reading position as well as practical after zoom, relayout, manual reload, and automatic reload.

## Milestone 2: Document Navigation Upgrade

Goal: make long documents substantially easier to navigate.

- [x] Add a sidebar table of contents generated from headings.
- [x] Support nested heading hierarchy and skipped heading levels.
- [x] Highlight the current section while scrolling.
- [x] Scroll long outlines independently and keep the active document section visible in the outline.
- [x] Let users resize the outline with a draggable divider and persist its width.
- [x] Click outline items to jump to headings.
- [x] Add keyboard navigation for the outline.
- [x] Add a command and shortcut to show/hide the outline.
- [x] Decide Back/Forward behavior for internal heading jumps: keep Back/Forward file-only; heading jumps do not enter history.
- [x] Support duplicate headings, Unicode headings, emoji, punctuation, and percent-decoded heading fragments.
- [x] Close the remaining GitHub-anchor gaps in duplicate suffix numbering and non-ASCII case folding.
- [x] Decide copy-heading-link support: out of scope for now.

## Milestone 3: Safer Links and Better File Context

Goal: make link behavior predictable, inspectable, and safer.

- [x] Reject suspicious or unsupported URL schemes such as `javascript:`, custom app protocols, and shell-like targets instead of passing them to the platform shell.
- [x] Warn before opening executable local files.
- [x] Show clear feedback for broken local files and document-section links.
- [x] Add document/background context menu actions for reload, copy document path, and open in file manager.
- [x] Add link context menu actions for opening the target in the file manager when the target is local.
- [x] Add a manual reload command and shortcut.
- [x] Preserve scroll position after manual or automatic reload when practical.
- [x] Add remembered scroll position per recent file.

## Milestone 4: Markdown Compatibility Improvements

Goal: improve fidelity for real-world documentation without turning the app into a browser.

Status: complete.

- [x] Pass original Markdown bytes to md4c without a preprocessing pass that can mutate fenced code.
- [x] Represent emphasis, strong, inline code, and strikethrough as combinable flags independent of links, images, and syntax roles.
- [x] Preserve soft and hard breaks separately; render/copy/search soft breaks as whitespace and hard breaks as explicit newlines.
- [x] Add parser, layout, renderer-font, search, copy-text, and hit-testing regressions plus a manual before/after fixture.
- [x] Add front matter detection for YAML, TOML, and conservatively recognized JSON metadata at the start of a file.
- [x] Decide whether front matter is shown, collapsed, or hidden by default: show title, author, date, and tags in a compact bar; hide other raw fields.
- [x] Add a deliberate raw HTML policy: render a narrow browser-free allowlist and retain unsupported input as visible source.
- [x] Support GitHub-style `<p>` and `<h1>`-`<h6>` alignment plus native `<a>`, `<img>`, and `<br>` rendering, including requested image dimensions.
- [x] Extend the safe subset with native `<kbd>`, `<sub>`, `<sup>`, and interactive `<details>` / `<summary>` rendering.
- [x] Show unsupported or unsafe HTML such as scripts, event handlers, iframes, arbitrary attributes, and external embeds as source without executing it.
- [x] Decide footnote support: defer it because the pinned md4c parser has no footnote extension; do not add a fragile source-preprocessing dialect.
- [x] Render GitHub-style `[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, and `[!CAUTION]` alerts with native colors, titles, and icons.
- [x] Expand recognized Markdown-related extensions with `.mdown` and `.mkd`; defer `.mdx` until an explicit partial/fallback mode is designed.

## Milestone 5: Tables, Images, and Copy Fidelity

Goal: polish the most visible rendering and copy edge cases.

Status: complete.

- [x] Keep fenced-code source lines intact, clip them to the block, and provide per-block horizontal scrollbars plus Shift+wheel/touchpad scrolling.
- [x] Reduce document-side padding responsively below a 900 px content viewport, reaching compact 12 px margins at 480 px and below.
- [x] Add per-table horizontal scrolling for tables whose useful column widths exceed the viewport.
- [x] Preserve column alignment and bounded cell wrapping for narrow or long-cell tables.
- [x] Add table context actions for copying as TSV or CSV.
- [x] Wrap very long unbroken strings and URLs at valid UTF-8 boundaries instead of letting them escape the document viewport.
- [x] Add image context menu actions for open image, copy image path, and open image in the file manager.
- [x] Decide image lightbox support: out of scope; rendered images already expose explicit open-image behavior without another in-app viewing mode.
- [x] Decide remote image policy: never fetch remote images automatically; show a labeled native placeholder instead.
- [x] Decide opt-in remote image loading: out of scope; retain privacy-preserving placeholders and explicit external open-image actions.
- [x] Render constrained local SVG through Skia without external resources; document that embedded HTML `<foreignObject>` content is unsupported.

## Milestone 5a: Native Complex Text, BiDi, and RTL

Goal: render and interact with Arabic, Hebrew, mixed-direction text, and other
complex scripts through one native shared text pipeline.

Status: implementation and Linux validation complete. A newly built Windows
Skia bundle and Windows host validation remain required before release.

- [x] Enable the pinned Skia HarfBuzz, SkShaper, SkUnicode, and ICU stack with
  explicit runtime availability checks and packaged ICU data on Windows.
- [x] Cache shaped glyphs, visual runs, BiDi levels, clusters, and safe caret
  geometry in the shared document layout instead of shaping during painting.
- [x] Preserve logical UTF-8 order for selection, exact search, copying, links,
  history, table export, and heading anchors while rendering visual BiDi order.
- [x] Apply direction-aware wrapping and alignment to paragraphs, headings,
  lists, code, tables, details, blockquotes, alerts, and outline labels.
- [x] Mirror RTL list markers, task boxes, blockquote accents, alert headers,
  details controls, and nested indentation without changing existing LTR layout.
- [x] Reuse shaped layout and glyph rendering for screen, native printing, and
  PDF export.
- [x] Add the mixed-direction manual fixture plus automated coverage for runtime
  initialization, shaping, wrapping, formatting, nested lists, hit testing,
  selection/search geometry, decorations, outline labels, print, PDF, and the
  ordinary soft-break wrapping regression found during integration.
- [x] Complete normal, ASan, UBSan, application smoke, and archive validation on
  Linux.
- [ ] Build and publish the replacement Windows Skia bundle, activate it in a
  separate commit, and complete the Windows build/test/manual matrix before the
  next application release.

Deliberate boundary: compact front-matter metadata tags and custom application
chrome still use their specialized LTR UI layout. The document surface and its
heading outline use the complex-text pipeline. Unicode-aware case-insensitive
search is completed in Milestone 6 and preserves the logical ranges used by the
complex-text selection and highlight geometry.

## Milestone 6: Search and Keyboard Usability

Goal: make search and keyboard workflows closer to expected desktop behavior.

Status: complete with a deliberately minimal search surface.

- [x] Keep case-sensitive search out of the current UI; reconsider only if real usage demonstrates a need.
- [x] Keep whole-word search out of the current UI; reconsider only if real usage demonstrates a need.
- [x] Keep regex search out of scope for the focused document-viewer experience.
- [x] Use Unicode case folding for case-insensitive search while preserving original UTF-8 byte ranges for highlighting and copy behavior.
- [x] Add `Ctrl+A` select-all support for the document surface.
- [x] Add a Reset Zoom command and `Ctrl+0` shortcut.
- [x] Keep the already-implemented outline keyboard navigation in Milestone 2; defer keyboard focus for links and top-bar controls to the accessibility work in Milestone 7.
- [x] Defer additional visible focus treatment until link/top-bar keyboard navigation provides actual focus targets.
- [x] Review and align shortcuts for open, find, find next/previous, copy, select all, reload, zoom reset, back/forward, print, and outline toggle on Windows and Linux.
- [x] Add shared regressions for Unicode search ranges and keyboard-command routing.

## Milestone 7: Themes and Accessibility

Goal: make the custom-rendered UI safer for everyday and accessibility use.

- [ ] Add system theme mode.
- [ ] Add high-contrast theme.
- [ ] Check color contrast for body text, links, code, syntax highlighting, search highlights, selection, blockquotes, tables, and disabled menu items.
- [ ] Avoid conveying link/search/focus state by color alone.
- [ ] Respect platform high-contrast settings where practical.
- [ ] Improve text scaling behavior and verify layout does not break at zoom bounds.
- [ ] Add keyboard access for document links and top-bar controls if it can be done without introducing a general widget framework.
- [ ] Add visible focus indication together with those keyboard-focus targets.
- [ ] Define what accessibility support is feasible for a Skia-rendered document surface, especially headings, links, tables, and focus state.

## Milestone 8: Print and Export

Goal: support common output workflows without compromising the viewer architecture.

- [x] Add native print support.
  - [x] Add Windows native print dialog support.
  - [x] Add Linux GTK print dialog support using the shared paginated renderer.
- [ ] Add an in-app print preview that uses the shared paginated renderer before opening the native print dialog.
- [x] Add export to PDF.
- [ ] Use a print-friendly light palette by default, independent of the active dark theme.
- [ ] Preserve links in exported PDF if practical.
- [ ] Handle page breaks for headings, paragraphs, code blocks, tables, and images.
- [x] Omit interactive horizontal scrollbars from output and shrink wide tables/code blocks within a readability floor before clipping.
- [ ] Avoid clipping wide tables and long code lines.
- [ ] Include optional document title and page numbers if implementation cost is reasonable.

## Milestone 9: Performance and Large Files

Goal: keep the app responsive with large or generated Markdown files.

- [x] Add an initial hard 64 MiB file-size safety limit before reading document contents.
- [ ] Add softer file-size and content-complexity thresholds for warnings or degraded mode.
- [ ] Avoid blocking the UI during expensive image preloading, syntax highlighting, search, or layout where practical.
- [ ] Consider incremental or cancellable layout for very large documents.
- [ ] Add graceful feedback for files too large to render comfortably.
- [ ] Cache render artifacts where useful without producing stale output after reload.
- [x] Cache syntax-highlighting results by canonical language and exact source content so relayout does not reparse unchanged code blocks.
- [x] Resolve sorted, overlapping syntax-highlight ranges in one linear pass after sorting.
- [x] Cancel unusually slow syntax-highlighting work and keep the code block readable as visibly marked plain text.
- [ ] Add performance fixtures for huge tables, long code blocks, many headings, many links, and many images.

## Milestone 10: Linux Hardening

Goal: harden the second native host and keep it aligned with the shared viewer behavior.

- [x] Pin Skia source builds to one recorded revision and document the locally produced Windows bundle workflow and provenance requirements.
- [x] Add Linux configure/build/test CI plus AddressSanitizer and UndefinedBehaviorSanitizer test coverage.
- [x] Make Linux PDF support explicit and keep non-PDF Skia builds linkable with the PDF command hidden.
- [x] Add exception boundaries around GLFW callbacks and the Linux update/render loop.
- [x] Use RAII for the Linux Skia GPU context and remove the duplicate leaked font-system owner.
- [x] Establish logical window coordinates for Linux layout/input and scale Skia drawing to framebuffer pixels.
- [x] Add initial image dimension/pixel/cache limits and clear document image caches on file changes.
- [x] Stop md4c callback exceptions at the C boundary, check parser failures, and cap Markdown nesting depth.
- [ ] Keep Linux build and runtime startup validated.
- [ ] Validate Linux file open dialog, clipboard, shell open, drag/drop if supported, menu commands, font selection, Skia surface creation, DPI behavior, and context menus.
- [ ] Match shared viewer behavior with Windows for opening files, links, search, selection, copy, themes, zoom, recent files, and history.
- [x] Add a stripped Linux archive with statically linked fetched dependencies, checksum, desktop metadata, CI artifact upload, and packaging notes.
- [x] Add a repeatable Linux release and manual smoke-test checklist.
- [ ] Execute and record the full manual Linux smoke-test checklist on representative X11, Wayland, and HiDPI desktops.
- [ ] Do not introduce platform-only product behavior unless required by native platform conventions.

## Later Backlog

- [ ] Native browser-free Mermaid rendering for fenced `mermaid` code blocks.
  - [ ] Do not use Chromium, Puppeteer, a webview, or the Mermaid CLI rendering pipeline.
  - [ ] Prefer a lightweight native integration, likely via a Rust static library/C ABI bridge using a mature browser-free renderer such as `mermaid-rs-renderer`.
  - [ ] Render into an app-native image surface using the now-enabled constrained Skia SVG path or a raster image fallback.
  - [ ] Cache rendered diagrams by source hash.
  - [ ] Fall back to the original code block plus an error message when rendering fails.
  - [ ] Defer this until the Rust/Cargo build dependency and CI/release impact are acceptable.
- [x] Native browser-free math rendering for Markdown.
  - [x] Recognize md4c `$...$` and `$$...$$` math spans without a Markdown source-rewriting pass.
  - [x] Keep normal prices, shell variables, unmatched dollar signs, and dollar-wrapped prose as ordinary Markdown text through conservative inline-math classification.
  - [x] Aggregate multiline md4c callbacks into one formula so environments such as matrices remain intact.
  - [x] Render inline and centered display mathematics through pinned MicroTeX and the shared Skia renderer on Windows and Linux.
  - [x] Support common mathematical notation including fractions, roots, scripts, large operators, matrices, accents, and scalable delimiters.
  - [x] Preserve copyable formula source with delimiters for selection, search, copying, links, and atomic hit testing.
  - [x] Reuse the shared Skia formula renderer for normal viewing, print, and PDF output.
  - [x] Bound formula source size/nesting and retain unsupported or failed formulas as visible source fallback.
  - [x] Package the required math fonts and their license notices with application runtime resources.
  - [x] Add parser, layout, drawing, interaction, ordinary-dollar, multiline-matrix, and runtime regression coverage plus a manual fixture.
- [ ] Definition lists.
- [ ] Wiki links.
- [ ] Command palette.
- [ ] Session restore.
- [ ] Optional focus mode.
- [ ] Sticky table headers.
- [ ] More code language grammars if demand justifies the build size and dependency impact.

## Explicit Product Decisions Still Needed

- [ ] Which Markdown dialect is the primary compatibility target: CommonMark, GitHub-flavored Markdown, documentation-site Markdown, or broad tolerant Markdown?
- [x] Should raw HTML render, be sanitized, or be shown as source? Use a strict native allowlist and show everything else as source.
- [x] Should front matter be visible, collapsed, or hidden by default? Show common fields in a compact visible bar and hide the remaining raw fields.
- [ ] Should remote images ever load automatically?
- [x] Should local links outside the current document tree require confirmation? No; warn only for executable local files.
- [ ] Should print/export preserve the active theme or always use a print-friendly theme?
- [ ] What level of accessibility is expected for a custom-rendered document surface?
- [ ] Should diagrams become a first-class feature or stay an optional enhancement?
- [x] Should math become a first-class feature or stay an optional enhancement? Treat native math rendering as a built-in Markdown feature with conservative recognition and source fallback.
