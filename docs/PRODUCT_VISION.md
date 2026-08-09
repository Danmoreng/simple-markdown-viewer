# Simple Markdown Viewer Product Vision and Feature Reference

## 1. Purpose and Product Scope

This document defines the durable product vision for Simple Markdown Viewer. It
describes the intended user experience, compatibility expectations, and feature
surface without acting as a second implementation backlog.

The application is a native, read-only, single-window Markdown viewer. Users can
open, browse, search, inspect, print, and export documents, but cannot edit them.
It remains browser-free and uses the shared C++/Skia document stack rather than a
webview or general widget framework.

Windows and Linux are the current host platforms. macOS remains a future target
that should reuse the shared parser, document model, layout, rendering,
interaction, configuration, and controller layers behind a thin native host.

Current milestones and implementation status belong in
[`../DEVELOPMENT_PLAN.md`](../DEVELOPMENT_PLAN.md). Repository-specific Linux
stability findings belong in
[`LINUX_STABILITY_AND_MARKDOWN_AUDIT.md`](LINUX_STABILITY_AND_MARKDOWN_AUDIT.md).

## 2. Product Principles

A good Markdown viewer should feel trustworthy, predictable, fast, and comfortable for long-form reading.

The most important principle is **faithful rendering without surprise**. Users should be confident that the document they are viewing looks close to how it would appear on common Markdown platforms, while still benefiting from a native desktop reading experience.

Secondary principles:

- **Read-first experience:** optimize for comprehension, navigation, and visual clarity.
- **Safe by default:** opening arbitrary Markdown files should not execute unsafe behavior.
- **Works with real-world Markdown:** tolerate imperfect, mixed-dialect, and repository-style documents.
- **Native desktop polish:** respect platform conventions without fragmenting the core experience.
- **Low friction:** opening a file should be instant, obvious, and recoverable.
- **One focused document window:** links may replace the current document and
  participate in history, but tabs, editing workspaces, and multi-document UI are
  outside the product scope.

### Current Product Baseline

The current application already provides:

- shared Markdown parsing, layout, rendering, selection, search, navigation, and
  configuration across Windows and Linux;
- headings, paragraphs, lists, blockquotes, thematic breaks, tables, task lists,
  strikethrough, links, local images, and fenced code blocks;
- Tree-sitter syntax highlighting for the supported code languages and code-copy
  buttons;
- document outline navigation, file history, recent files, link hover previews,
  native file dialogs, drag and drop, themes, runtime font choice, and reader
  zoom;
- PDF export, plus native print support on Windows;
- Windows live reload when the current file changes.

The current baseline is not considered complete. Linux crash hardening, resource
limits, Markdown correctness fixes, safe raw HTML handling, accessibility, and
large-document behavior take precedence over broad feature expansion.

## 3. Core User Jobs

A read-only Markdown viewer should support these primary jobs well:

1. Open a local Markdown file and read it comfortably.
2. Navigate a long document quickly.
3. Verify structure, links, images, tables, code blocks, and diagrams.
4. Search inside the document.
5. Open related local or remote references safely.
6. Copy selected content accurately.
7. Export or print when needed.
8. Compare the rendered view against expectations from common Markdown ecosystems.

## 4. Essential File Handling Features

### 4.1 Supported File Types

Current behavior:

- `.md`
- `.markdown`
- known text formats and extensionless files that pass the text probe

Planned compatibility additions:

- `.mdown`
- `.mkd`
- `.mdx` only as an explicit partial/fallback mode; JSX or JavaScript must never
  execute

### 4.2 Opening Documents

Currently supported open flows:

- Open via file picker.
- Drag and drop onto window.
- Open from command line or shell association.
- Open recent files.

Future platform integration may add drag-to-app-icon behavior, session restore,
and clearer handling for files moved, deleted, or renamed since the previous
session.

### 4.3 File Association

On desktop platforms, users often expect Markdown files to open with their chosen viewer. Packaging should support:

- Registering as an optional Markdown file handler.
- Using appropriate file icons.
- Respecting platform conventions for “Open With.”

Opening a second document replaces the current document in the same window and
updates file navigation history. Tabs and multi-window document management are
not part of this product.

### 4.4 Encoding and Line Endings

Current behavior includes UTF-8 sanitization and UTF-8 BOM removal. The long-term
compatibility target includes:

- UTF-8.
- UTF-8 with BOM.
- Clear feedback or deliberate conversion for unsupported legacy encodings.
- Windows, Unix, and old Mac line endings.
- Mixed line endings.
- Unicode symbols, emoji, CJK text, RTL scripts, and combining characters, with
  current limitations documented where the Skia text stack lacks shaping or bidi
  support.

### 4.5 Large Files

Even though Markdown files are usually small, real-world logs, exported docs, generated API references, and changelogs can be large. Desired behavior:

- Open large files without freezing the UI.
- Show progress or partial rendering for very large files.
- Provide useful feedback if a file is too large to render comfortably.
- Search large documents without locking the app.

## 5. Markdown Rendering Features

Rendering is the heart of the product. A viewer does not need to support every obscure extension, but it should be explicit, consistent, and compatible with common expectations.

### 5.1 Baseline Markdown Support

The current md4c pipeline supports the following CommonMark-style features:

- Headings `#` through `######`.
- Paragraphs and line breaks.
- Bold, italic, bold italic.
- Inline code.
- Code blocks with fences.
- Indented code blocks.
- Blockquotes.
- Ordered lists.
- Unordered lists.
- Nested lists.
- Thematic breaks.
- Inline links.
- Reference links.
- Images.
- Escaped characters.
- HTML entity decoding.

Combined/nested inline styles, soft versus hard line breaks, and fenced-code
preservation are covered by the current shared model and regression suite.
Additional heading-anchor compatibility remains separate follow-up work.

### Importance

This is the minimum bar. If these are wrong, users will distrust the viewer.

### 5.2 GitHub-Flavored Markdown Features

These are highly desirable because many Markdown files are written for GitHub, GitLab, Bitbucket, Azure DevOps, or similar tools.

Currently supported GFM features:

- Tables.
- Task lists with checked and unchecked boxes.
- Strikethrough.
- Autolinks.
- Fenced code language tags.
- Heading anchors.

GitHub alerts are supported for `NOTE`, `TIP`, `IMPORTANT`, `WARNING`, and
`CAUTION`. Footnotes remain a planned compatibility addition.

Task list checkboxes should be visibly read-only. Users should not think they can toggle them unless editing is intentionally supported elsewhere.

### 5.3 Tables

Tables deserve special attention because poor table rendering is immediately noticeable.

Currently supported table behavior:

- Header rows.
- Column alignment.
- Inline formatting inside cells.
- Code inside cells.
- Links inside cells.
- Images inside cells where the shared image path can resolve them.
- Bounded useful column widths with per-table horizontal scrolling when the
  table is wider than the viewport.
- Safe UTF-8 boundary wrapping for very long unbroken cell content.

Remaining table work:

- Sticky table header, optional for long tables.
- Copy table as plain text, Markdown, or tab-separated text.
- Clear behavior for malformed tables.

Easy-to-miss table issues:

- Very long unbroken strings.
- Narrow columns.
- Tables wider than the viewport.
- Nested inline elements.
- Escaped pipe characters.
- Empty cells.
- Alignment markers.

### 5.4 Code Blocks

Code rendering is one of the most important quality signals for technical users.

Currently supported code block features:

- Monospace font.
- Preserved indentation.
- Syntax highlighting.
- Language label display.
- Copy code block button.
- Copy code block without extra prompts or line numbers.
- Supported aliases for C, C++, JavaScript, TypeScript, TSX, JSON, Python, Bash,
  Rust, Go, and C#.
- Graceful fallback for unknown languages.
- Preserved source lines with clipped, per-block horizontal scrolling for long code.

Remaining work includes additional grammars only where justified and optional
line numbers if they do not compromise copy fidelity.

Code block usability matters because many Markdown files are README files, runbooks, changelogs, design docs, or API documentation.

### 5.5 Inline Code

Inline code should:

- Use a visually distinct monospace style.
- Avoid breaking layout awkwardly.
- Handle long inline snippets.
- Preserve literal characters accurately.
- Remain readable in dark and light themes.

### 5.6 Links

Link handling should be safe, clear, and predictable.

Currently supported link behavior:

- Clearly styled links.
- Hover preview for URL destination.
- Open external links in the default browser.
- Open local relative links inside the viewer when possible.
- Support links to headings in the same document.
- Support links to other local Markdown files.
- Support links to images, PDFs, plain text, and other common local assets.
- Handle percent-encoded paths and spaces.

Remaining link work includes explicit suspicious-scheme handling, warnings for
executable or unrelated external local paths, visible broken-link feedback, and
well-defined case-sensitive path behavior across current and future hosts.

Easy-to-miss link cases:

- Relative links using `./`, `../`, or root-like repository paths.
- Links with spaces.
- Links to headings generated by platform-specific slug rules.
- Links to files without extensions.
- Links to local paths that do not exist.
- Anchors with duplicate heading names.
- Unicode heading anchors.
- Links containing parentheses.

### 5.7 Images

Image support is essential for many documents.

Currently supported image behavior:

- Render local raster and SVG images referenced by relative paths.
- Respect alt text.
- Show placeholder for missing or failed images.
- Fit images to content width by default.
- Enforce source-size, decoded-pixel, rendered-dimension, and cache limits.

SVG rendering uses Skia's browser-free SVG module and does not load external
resources. Embedded HTML/CSS through SVG `<foreignObject>` is not supported;
exports from tools such as tldraw should convert text to SVG paths/text or use a
raster README image. Remote images are not loaded.

Future image features may include opening or copying an image, revealing its
path, and a single-window lightbox.

Security and privacy considerations:

- Remote image loading can leak network information. A read-only viewer should offer a clear setting for remote content.
- SVG can contain active or external content depending on renderer behavior. Treat it carefully.

### 5.8 Raw HTML in Markdown

Many Markdown files contain embedded HTML. The viewer implements a small native
allowlist rather than browser-compatible HTML. Centered paragraphs and headings,
links, local images with width/height hints, and `<br>` are translated into the
same document model used by Markdown. Unknown or unsafe fragments remain visible
as source.

Planned user-facing behavior:

- Already render `<br>`, `<a>`, and `<img>` plus aligned `<p>` and `<h1>`-`<h6>` blocks.
- Consider `<kbd>`, `<sub>`, `<sup>`, `<details>`, and `<summary>` as later allowlist extensions.
- Render common block tags such as `<table>` only if safe and consistent.
- Never execute scripts, event handlers, iframes, external embeds, or unsafe
  attributes.
- Keep unsupported HTML visible as source so suppression is never silent.

Easy-to-miss HTML features:

- `<details>` / `<summary>` collapsible sections.
- `<kbd>` keyboard shortcut styling.
- `<sup>` and `<sub>` for footnote-like content.
- HTML comments.
- Raw HTML tables.
- Inline styles are ignored because arbitrary document styling conflicts with
  app themes and the browser-free rendering model.

### 5.9 Front Matter

Markdown files often include YAML, TOML, or JSON front matter.

Front matter is not currently detected and remains planned work.

Desirable behavior:

- Detect front matter at the beginning of the file.
- Render it in a visually distinct metadata block, collapse it, or hide it based on user preference.
- Avoid treating front matter as a thematic break by mistake.
- Provide copy support.

This is easy to miss because many viewers are tested mostly with README files, while documentation sites and static-site generators use front matter heavily.

### 5.10 Footnotes

Footnotes are common in documentation and writing workflows.

Footnotes are not currently supported and remain a high-value GitHub
compatibility addition.

Desired features:

- Render footnote references clearly.
- Navigate from reference to footnote and back.
- Support multiple references to the same footnote.
- Handle long footnotes and nested formatting.

### 5.11 Definition Lists

Definition lists are not universal Markdown, but they appear in some ecosystems.

They are not currently supported and remain optional broad-compatibility work.

Desirable if targeting broad compatibility:

- Term and definition formatting.
- Nested content support.
- Graceful fallback if unsupported.

### 5.12 Math

Math support may be optional, but it is valuable for technical, academic, and product documents.

Math is not currently rendered and should remain an optional enhancement with a
safe source fallback.

Desirable features:

- Inline math.
- Block math.
- Clear fallback if math rendering is disabled or unsupported.
- Copy math source.
- Avoid breaking normal dollar-sign text.

Important edge cases:

- Currency values using `$`.
- Escaped dollar signs.
- Multi-line equations.
- Documents that mix Markdown tables and math.

### 5.13 Diagrams

Diagram support is a major usability perk, especially for technical documentation.

Diagrams are not currently rendered. Mermaid is retained as a later native,
browser-free enhancement; PlantUML and Graphviz are not committed roadmap items.

Possible supported diagram types:

- Mermaid.
- PlantUML.
- Graphviz DOT.

Recommended product behavior:

- Treat diagrams as optional enhanced rendering.
- Show source fallback when rendering fails.
- Provide copy diagram source.
- Provide zoom or open-in-large-view.
- Avoid hidden network calls unless clearly configured.

### 5.14 Emoji and Shortcodes

Markdown documents often use emoji directly or via shortcodes.

Unicode emoji currently use Skia font fallback. Shortcode expansion is not
implemented. Desired behavior:

- Render Unicode emoji correctly.
- Optionally support `:shortcode:` syntax for common platform compatibility.
- Avoid replacing text unexpectedly in code blocks or inline code.

### 5.15 Mentions, Issue Links, and Platform-Specific Syntax

Repository README files may contain platform-specific syntax.

Examples:

- `@username`
- `#123`
- `GH-123`
- Commit hashes.
- GitHub alerts such as `[!NOTE]`, `[!TIP]`, `[!WARNING]`.
- Wiki links like `[[Page Name]]`.

These do not all need full support, but the viewer should handle them gracefully.
GitHub-style alerts are rendered natively with a colored accent, title, and icon.

Repository-context expansion for usernames, issue numbers, or commit hashes is
not currently planned because the viewer has no repository or network identity
context. GitHub alerts remain entirely document-local and require no repository
or network context.

## 6. Document Navigation Features

### 6.1 Table of Contents

A generated table of contents is one of the most useful features for long documents.

The current viewer provides an outline sidebar generated from headings, nested
level indentation, current-section highlighting, click navigation, keyboard
navigation, configurable left/right placement, and a collapsed sidebar state.
Remaining work is limited to edge-case correctness, accessibility, and an
optional collapsible heading hierarchy if later justified beyond collapsing the
whole sidebar.

Easy-to-miss behavior:

- Duplicate headings.
- Headings inside blockquotes or HTML.
- Very long headings.
- Emoji in headings.
- RTL headings.
- Documents with no headings.

### 6.2 In-Document Anchors

Current behavior supports heading links in the same document and navigation to
fragments in linked local documents. File Back/Forward history deliberately does
not include individual heading jumps.

Possible future behavior:

- Copy link to heading, if the single-window local-file semantics are clear.

### 6.3 Search Within Document

Search is essential. The current viewer searches rendered plain text, highlights
all matches, displays a match count, supports next/previous navigation, and
searches text from code blocks and tables.

Remaining search enhancements:

- Case-sensitive toggle.
- Whole-word toggle.
- Regex search, optional.
- Search inside hidden or collapsed sections, with clear behavior.
- Search results should scroll to the exact visible match.

Easy-to-miss search cases:

- Text split across inline formatting nodes.
- Unicode normalization.
- Accented characters.
- Case folding in non-English languages.
- Matches inside tables.
- Matches inside generated or hidden content.

### 6.4 Back and Forward Navigation

A viewer benefits from browser-like navigation. Current history covers opened
files and local linked documents. Heading jumps do not enter history by design.

- Back after opening a relative local document.
- Forward after going back.
- Restore per-file scroll position when returning, as future work.

This is easy to miss and makes long-document reading much less frustrating.

### 6.5 Scroll Position Memory

Relayout, zoom, and reload already preserve the visible reading position where
practical. Remaining behavior:

- Remember scroll position per file.
- Restore position when reopening recent files.
- Optionally remember zoom level and sidebar state.
- Handle changed files without restoring to a nonsensical position.

## 7. Reading and Visual Design Features

### 7.1 Typography

A good viewer should make Markdown pleasant to read.

The current renderer provides shared typography, heading hierarchy, document
spacing, blockquote/list/table/code styling, runtime font selection, and reader
zoom. Further work should focus on fallback shaping, accessibility, and difficult
layout cases rather than adding a large typography-settings surface.

Desirable features:

- Clear typographic hierarchy for headings.
- Comfortable line height.
- Sensible maximum content width.
- Good paragraph spacing.
- Distinct blockquotes.
- Legible list indentation.
- Balanced spacing around tables, code blocks, images, and headings.
- High-quality default fonts per platform.
- User-adjustable font size.

### 7.2 Themes

Currently available:

- Light theme.
- Dark theme.
- Sepia reading theme.

Remaining work:

- Follow system theme.
- High-contrast theme.
- Separate code theme, or code theme matched to app theme.
- Keep theme selection global rather than adding per-document theme state.

### 7.3 Zoom

Current desktop behavior includes zoom in/out, keyboard shortcuts, Ctrl+mouse
wheel zoom, persistent base font size, and reading-position preservation during
relayout. Remaining behavior:

- Reset zoom.

### 7.4 Layout Options

These options are not currently implemented. Any addition should preserve the
focused single-window reader UI:

- Normal width.
- Wide layout.
- Full-width layout.
- Focus mode without sidebar or toolbar.
- Optional line wrapping behavior for code blocks.
- Optional compact or spacious density.

### 7.5 Print and Export

Even read-only viewers often need output features. PDF export is implemented.
Native print is implemented on Windows; Linux and future macOS print integration
remain platform work.

Desirable features:

- Print rendered document on all supported hosts.
- Export to PDF reliably on all supported hosts.
- Copy rendered selection.
- Copy selected text as plain text.
- Copy selected text as Markdown source, optional.
- Page-break handling for print.
- Print-friendly treatment of code blocks and tables.
- Include document title and page numbers, optional.

Easy-to-miss export issues:

- Dark theme printed with dark background by accident.
- Wide tables cut off in PDF.
- Code blocks clipped horizontally.
- Links not preserved in PDF.
- Images missing from exported output.

## 8. Interaction and Usability Perks

### 8.1 Copy Features

Currently supported copy actions include selected rendered text, link URLs, and
whole code blocks. Remaining useful actions:

- Copy image path or image.
- Copy heading link.
- Copy table as tab-separated values.
- Copy document path.

Copy fidelity matters. Users should not get surprising extra characters, broken whitespace, or hidden UI text.

### 8.2 Context Menus

The current native context menu supports selected text and link actions. Future
context-menu target surfaces may include:

- Images.
- Code blocks.
- Document background.
- Sidebar headings.

Already supported actions include opening and copying links and copying selected
text. Remaining useful actions:

- Reveal local file in file manager.
- Open image.
- Copy image.
- Reload document.

### 8.3 Keyboard Shortcuts

The current hosts cover open, find, find next/previous, copy, zoom in/out,
Back/Forward, and outline toggle through shared interaction behavior. Remaining
shortcut work includes:

- Zoom reset.
- Print.
- Reload.
- Select all.
- Open recent.
- Command palette, optional.

Platform conventions matter: shortcuts should feel native on Windows/Linux and macOS.

### 8.4 Single-Window Files and Recent Files

The product remains single-window. Opening a file or supported local document
link replaces the current view and participates in Back/Forward history. Recent
files are persisted; Windows exposes them in the current native menu, while Linux
menu parity remains follow-up work. Optional session restore may reopen one last
document, but must not grow into tabs or a workspace model.

### 8.5 File Watching and Reload

Even read-only viewers often display files being edited elsewhere. Windows
currently watches the active file and reloads it. Linux and future macOS need
equivalent native services, and all platforms still need a manual reload command
and clearer failure feedback.

Desirable behavior:

- Manual reload.
- Preserve scroll position after reload when possible.
- Show a non-intrusive notice if the file was deleted or moved.
- Handle partial writes gracefully.

This is easy to forget because the viewer itself does not edit files, but users may use it alongside an editor.

### 8.6 Breadcrumbs and File Context

Useful context features:

- Show file name.
- Show full path on hover or in details panel.
- Reveal in file manager.
- Copy file path.
- Show modified date and file size, optional.
- Show document title derived from first heading, optional.

### 8.7 Error and Empty States

Good empty/error states are part of usability.

Examples:

- No file open.
- File not found.
- Permission denied.
- Unsupported encoding.
- Very large file warning.
- Broken image placeholder.
- Broken local link feedback.
- Diagram rendering failed.
- Math rendering failed.
- Remote content blocked.

Each state should tell the user what happened and what they can do next.

## 9. Accessibility Features

Accessibility should be considered a core product feature, not a polish layer.

Important features:

- Keyboard-only navigation.
- Proper focus indicators.
- Screen-reader-accessible document structure.
- Semantic heading structure where possible.
- Accessible table navigation.
- Alt text exposure for images.
- High-contrast mode.
- Respect reduced-motion preferences.
- Text scaling without layout breakage.
- Sufficient color contrast for links, code, blockquotes, and search highlights.
- No information conveyed by color alone.
- Visible focus when navigating links and controls.

Easy-to-miss accessibility cases:

- Search highlights invisible in high-contrast mode.
- Code syntax colors with insufficient contrast.
- Link styling that relies only on color.
- Collapsible sections not keyboard accessible.
- Sidebar outline not navigable by keyboard.
- Focus lost after clicking an anchor.

## 10. Security, Privacy, and Trust Features

Even a viewer can expose users to unsafe or privacy-invasive content.

### 10.1 Remote Content

Markdown can reference remote images and links.

Current behavior and possible extensions:

- Remote images are never fetched automatically.
- A labeled native placeholder is shown when remote content is blocked.
- A future version may offer explicit per-document or global opt-in loading.
- Avoid automatic execution of embedded remote content.

### 10.2 Unsafe HTML and Scripts

A read-only Markdown viewer should not execute scripts from Markdown documents.

User-facing expectations:

- No script execution.
- No unsafe inline event handlers.
- No silent external embeds.
- No automatic file execution through links.
- Warnings for suspicious protocols.

Potentially dangerous link schemes:

- `javascript:`
- `file:` to sensitive locations
- custom app protocols
- shell-like or command-like URLs

### 10.3 Local File Access

Relative local links are useful but should be constrained and understandable.

Desirable behavior:

- Open related local files deliberately.
- Avoid surprising traversal into unrelated sensitive paths.
- Clearly display target path before opening, at least on hover or context menu.
- Warn on executable files.

## 11. Cross-Platform Native Expectations

Windows and Linux are current hosts. macOS is a future target. Platform hosts
must stay thin and provide native services while sharing document behavior.

### 11.1 Shared Expectations

Across all platforms:

- Fast startup.
- Native file picker.
- Drag and drop.
- Clipboard support.
- High-DPI rendering.
- Smooth scrolling.
- Correct font rendering.
- Native light/dark preference.
- System accent or selection behavior where appropriate.

### 11.2 Windows Expectations

The Windows host is currently the most complete platform implementation. It
already provides native dialogs, clipboard/shell integration, drag and drop,
recent-file menus, file watching, crash dumps, PDF export, and native printing.
Remaining expectations include:

- File association through “Open With.”
- Jump list or recent files, optional.
- Explorer “Reveal in folder.”
- Windows-style keyboard shortcuts.
- Proper scaling across monitors.
- Respect high-contrast accessibility settings.

### 11.3 macOS Expectations

macOS remains a future product target, not a current implementation claim. A
future host should provide:

- Standard menu bar behavior.
- Command-key shortcuts.
- Drag file onto dock icon.
- Reveal in Finder.
- Native full-screen behavior.
- Document proxy icon, optional.
- Recent documents integration, optional.

### 11.4 Linux Expectations

The Linux host exists and shares the main viewer stack. Immediate work is
stability, reproducible builds, CI, DPI correctness, crash diagnostics, and
behavioral parity before packaging polish. Longer-term expectations include:

- XDG file associations.
- Freedesktop desktop entry.
- Open containing folder via default file manager.
- Wayland and X11 friendliness.
- Respect system theme where available.
- Avoid assuming one desktop environment.

## 12. Performance and Responsiveness

The user-facing performance bar is simple: documents should feel instant unless they are unusually large or complex.

Desirable behavior:

- Fast initial render.
- Smooth scrolling.
- Responsive search.
- Responsive sidebar outline.
- No freezing during image loading, syntax highlighting, or diagram rendering.
- Graceful degradation for huge files.
- Avoid layout instability while content loads.
- Cache useful render artifacts where appropriate, without stale output.

Performance-sensitive content:

- Huge tables.
- Long code blocks.
- Many images.
- Many headings.
- Large generated API docs.
- Documents with thousands of links.
- Large Mermaid diagrams.

## 13. Settings and Preferences

Current persisted settings are named theme, document font family, reader zoom,
outline side, and recent-file metadata. The settings surface should stay small.

Potential additions only where they solve a demonstrated need:

- Content width.
- Code wrapping.
- Remember last session.
- Auto-reload changed files.
- Load remote images.
- Enable or disable diagrams.
- Enable or disable math.
- Default link opening behavior.
- Print/export preferences.

Settings should be understandable to non-technical users. Avoid exposing parser jargon unless the target audience is explicitly technical.

## 14. Compatibility Expectations

Users may compare the viewer against existing Markdown platforms.

High-value compatibility targets:

- CommonMark.
- GitHub-Flavored Markdown.
- GitLab-style Markdown conventions.
- Static-site-generator Markdown with front matter.
- Documentation-site Markdown with admonitions or alerts.
- README files from software repositories.

Nice-to-have compatibility:

- Obsidian-style wiki links.
- Jupyter-exported Markdown.
- Pandoc-style extensions.
- Markdown with embedded HTML.
- Markdown files generated from documentation tools.

The proposed compatibility direction is CommonMark/GFM-oriented rendering with
tolerant, safe fallback for real-world repository and documentation files. The
exact dialect contract remains an explicit product decision. Unsupported syntax
must degrade gracefully and must never execute active content.

## 15. Most Important Rendering Features

The current renderer already covers the core Markdown blocks, GFM tables, task
lists, strikethrough, links, local raster images, and syntax-highlighted fenced
code. The most important remaining quality work is:

1. Prevent crashes and unbounded resource use for images, large files, deep
   nesting, large tables, and large code blocks.
2. Completed: correct fenced-code mutation, nested inline styles, and soft line
   breaks. Heading anchors cover the common cases, with duplicate suffix numbering
   and non-ASCII case folding retained as explicit follow-up work.
3. Completed: preserve fenced-code source lines, add per-code/table horizontal
   scrolling, and safely wrap long unbroken tokens.
4. Completed for the common GitHub header pattern: implement a strict native
   HTML subset for aligned paragraphs/headings, links, images, and line breaks.
5. GitHub alerts are complete; add front matter and footnotes.
6. Treat math and diagrams as optional enhancements with source fallback.

## 16. Highest-Value Usability Perks

The viewer already includes an outline, in-document search, file history, code
copy, internal local links, link hover previews, recent files, runtime fonts,
reader zoom, themes, PDF export, and Windows live reload/printing.

The highest-value remaining usability work is Linux runtime validation and parity,
remaining HTML extensions, remembered per-file scroll positions, manual reload,
broken-resource feedback, keyboard focus/accessibility, system/high-contrast themes,
and print/export polish.

## 17. Easy-to-Miss Features and Edge Cases

This section is the “forgotten obvious things” checklist.

### Markdown and Rendering

- Escaped pipe characters in tables.
- Tables wider than the viewport.
- Long unbroken words or URLs.
- Nested lists with mixed ordered and unordered items.
- Task lists nested inside lists.
- Duplicate heading anchors.
- Unicode and emoji in headings.
- Raw HTML such as `<details>`, `<summary>`, `<kbd>`, `<sup>`, `<sub>`.
- Front matter being rendered incorrectly as a horizontal rule.
- Dollar signs mistaken for math.
- Code fences with uncommon language aliases.
- Code fences with no language.
- Code fences nested in lists or blockquotes.
- Images with spaces or encoded characters in paths.
- Missing alt text.
- Broken local relative links.
- Links containing parentheses.
- Reference-style links.
- Footnote back-navigation.
- HTML comments.
- Windows paths in Markdown.
- Case-sensitive path differences across platforms.

### Navigation

- Back button after clicking a heading anchor.
- Restoring scroll position after reload.
- Search matches split across styled spans.
- Search inside tables and code blocks.
- Current heading highlight in the outline.
- Behavior for documents with no headings.
- Behavior for extremely long headings.

### File Handling

- UTF-8 with BOM.
- Non-UTF-8 files.
- Files changing while open.
- File deleted while open.
- File renamed while open.
- Permission denied.
- Very large Markdown files.
- Relative links after opening a file via symlink.
- Opening from command line.
- Opening files from network drives or cloud-synced folders.

### Desktop Usability

- Native print behavior.
- Export to PDF preserving links.
- Copy from rendered view without weird spacing.
- Copy code without line numbers.
- Reveal file in system file manager.
- Drag and drop onto window and app icon.
- Recent files.
- Multi-monitor DPI changes.
- Dark theme printing accidentally.
- High-contrast mode.
- Keyboard-only operation.

### Security and Privacy

- Remote images leaking network requests.
- Unsafe `javascript:` links.
- SVG safety.
- Raw HTML scripts or event handlers.
- Custom protocol links.
- Executable local file links.
- External links that look different from their real target.

## 18. Suggested Feature Priority

Detailed ordering and completion state are maintained only in
[`../DEVELOPMENT_PLAN.md`](../DEVELOPMENT_PLAN.md). At the product level, work is
grouped into three horizons:

### Immediate

- Linux stability, build reproducibility, CI, crash diagnostics, and parity.
- Resource limits and graceful failure for images and complex documents.
- Markdown correctness regressions and regression fixtures.

### Next

- Additional safe HTML extensions where real documents justify them.
- Front matter and footnotes.
- Table copy fidelity, search, keyboard, and accessibility improvements.
- Cross-platform reload, print, and error-state polish.

### Later

- Browser-free Mermaid rendering and optional math.
- Definition lists, wiki links, command palette, focus mode, sticky table
  headers, image lightbox, and limited session restore.
- A thin native macOS host using the shared viewer architecture.

## 19. Product Questions to Decide Explicitly

Decided:

- The app is read-only, single-window, native, Skia-rendered, and browser-free.
- Supported local Markdown and text links open inside the current viewer unless
  the user explicitly forces external handling.
- File Back/Forward history does not record individual heading jumps.
- Theme configuration stores a named theme rather than arbitrary palette
  overrides.
- Math and diagrams remain optional later enhancements, not prerequisites for
  the core viewer.
- Windows and Linux are current platforms; macOS is a future target.

Still open:

1. What exact CommonMark/GFM compatibility contract should be promised?
2. Should remote images ever load automatically, and what privacy mode governs
   them?
3. Which safe HTML tags and attributes should render, and how should unsupported
   HTML appear?
4. Should front matter be visible, collapsed, or hidden by default?
5. Should print/export preserve the active theme or use a print-friendly palette?
6. What accessibility contract is feasible for a custom Skia-rendered document
   surface?
7. What deterministic fallback should be shown when output differs from GitHub
   or another reference renderer?

## 20. Recommended “Obvious but Often Forgotten” Checklist

Before considering the viewer complete, test these manually:

- A README with images using relative paths.
- A README with wide tables.
- A Markdown file with front matter.
- A file with nested task lists.
- A file with duplicate headings.
- A file with Unicode headings and emoji.
- A file with long code lines.
- A file with code fences inside lists.
- A file with broken local links.
- A file with broken images.
- A file with raw `<details>` blocks.
- A file with footnotes.
- A file with dollar signs that are not math.
- A file larger than expected.
- A file changed externally while open.
- A file opened from a path containing spaces.
- A file opened from a symlink or shortcut.
- Copying rendered text, code, links, and tables.
- Printing in light and dark mode.
- Search inside tables and code blocks.
- Keyboard-only navigation.
- High-DPI display behavior.
- Windows, macOS, and Linux file association behavior.

## 21. Summary

A strong read-only Markdown viewer is not just a parser with a pretty surface. The key product qualities are rendering trust, comfortable reading, fast navigation, safe link and content handling, and graceful behavior for messy real-world Markdown.

The current product already has a substantial reader foundation: CommonMark/GFM
rendering, tables and code blocks, local images and links, search, an outline,
themes, zoom, copy actions, history, recent files, and export. Product quality now
depends more on robustness and fidelity than on accumulating surface features.

The features and edge cases most likely to be missed are resource limits, Linux
crash diagnostics, front matter, cross-platform file watching, relative-link
security, duplicate heading anchors, copy fidelity, remaining HTML extensions,
remote-image privacy, high-contrast accessibility, and print/export behavior.
