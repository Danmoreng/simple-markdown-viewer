# Linux Stability and Markdown Compatibility Audit

Date: 2026-08-08

## Purpose

This audit records repository-specific risks found while investigating reports
that some Markdown documents can crash the Linux build. It also identifies the
highest-value Markdown compatibility work, especially for documents that render
well on GitHub but currently expose raw HTML in the viewer.

The findings below are based on source inspection and the existing Windows test
target. A crash dump or minimized failing document is still required to prove
which risk is responsible for a particular Linux crash.

## Validation Performed

- Fetched `origin/main` and confirmed that local `main` matched it at
  `cb465e4505283e3661c3365d1246f80ba4f55a91` before this document was added.
- Built the `mdviewer_tests` Release target on Windows.
- Ran CTest successfully: one test target, nine test groups, zero failures.
- Inspected the Markdown parser, document model, layout, rendering, image cache,
  Tree-sitter highlighting, shared interaction code, Linux host, build scripts,
  and CI configuration.
- Did not reproduce the Linux crash because no Linux runtime or failing fixture
  was available in the inspection environment.

## Highest-Priority Stability Risks

### 1. Unbounded image dimensions and cache growth

This is the strongest content-dependent crash candidate when affected documents
contain images.

- Layout preserves intrinsic aspect ratio without a maximum rendered height or
  pixel budget. Extremely tall or wide images can produce very large target
  dimensions.
- `DocumentImageCache::GetImage()` creates a raster surface at the requested
  target dimensions without first enforcing maximum width, height, total pixels,
  or bytes.
- The image cache stores the base image and every scaled variant.
- The cache is not cleared on document changes and has no LRU or memory budget.
- Missing or invalid image URLs also leave empty cache entries behind.

Recommended safeguards:

- Reject or downsample images above a decoded-pixel budget.
- Clamp render width, render height, and aspect ratio before integer conversion.
- Check for non-finite dimensions and integer overflow.
- Add an LRU or byte-budgeted cache and clear document-specific entries when a
  new file is opened.
- Use non-throwing filesystem APIs and return a safe placeholder on every image
  failure.

### 2. Missing exception boundaries in the Linux event path

File loading, image preloading, parsing, layout, and rendering are invoked from
GLFW callbacks and the main loop without a top-level C++ exception boundary.
Filesystem operations also use throwing overloads in several content-derived
paths. An exception escaping through a C callback can terminate the process.

Recommended safeguards:

- Catch exceptions at each GLFW callback boundary and around the main render
  iteration.
- Convert expected filesystem failures to `std::error_code` results.
- Track the active processing phase (`read`, `parse`, `preload`, `layout`,
  `render`) and current document path for diagnostics.
- Install a Linux terminate/crash-report path that records this context. Retain
  core dumps for native stack analysis.

### 3. No content size or complexity budgets

The full file is read into memory, sanitized into a second string, normalized
into another string, parsed into a document model, and expanded again into the
layout model. There are no thresholds for file size, block count, nesting depth,
table dimensions, image count, or code-block length.

Tree-sitter reparses supported fenced code blocks during every relayout. Capture
overlap filtering is quadratic in the number of highlight ranges. Large tables
also contain repeated row and column scans. These paths can cause excessive CPU
or memory use and may be perceived as crashes or trigger the Linux OOM killer.

Recommended safeguards:

- Add configurable hard and soft file-size limits.
- Add degraded mode for large documents: skip syntax highlighting, image
  preloading, outline generation, or expensive search highlighting as needed.
- Cap syntax-highlighting input and cache results by language plus source hash.
- Make table layout linear in cell count where practical.
- Report a clear error instead of attempting an unsafe layout.

### 4. Recursive traversal of document trees

Layout, rendering, hit testing, scroll-anchor lookup, printing, and image
preloading recurse through nested blocks. Pathologically nested Markdown can
therefore exhaust the native stack.

Recommended safeguards:

- Enforce a maximum parser/model nesting depth.
- Prefer iterative traversal for shared document walks.
- Add a deeply nested list/blockquote regression fixture.

### 5. Parser failures are not propagated

The return value from `md_parse()` is ignored. Parser callbacks allocate C++
containers and strings but do not convert allocation or callback failures into a
controlled parse status.

Recommended safeguards:

- Return an explicit parse result containing status and diagnostics.
- Catch failures inside the callback boundary and abort md4c cleanly.
- Do not install a partially constructed model as the current document.

## Linux Build and Platform Gaps

### Linux PDF build configuration is inconsistent

`build.sh` builds Skia with `skia_enable_pdf=false`, while the common Linux target
now includes `pdf_exporter.cpp`, which calls the Skia PDF backend. This can break
fresh Linux linking after the PDF feature was added.

Choose one explicit policy:

- enable Skia PDF support on Linux and validate PDF export in CI; or
- compile the PDF feature conditionally and disable its menu command when the
  backend is unavailable.

### Linux has no CI coverage

The repository currently has Windows build/release workflows only. Add an Ubuntu
job that at minimum configures, builds, and runs `mdviewer_tests`. A separate
Debug sanitizer job should run parser/layout regression fixtures without needing
a display server.

### Logical size and framebuffer size are mixed

The Linux surface uses GLFW framebuffer dimensions, while layout, scene
parameters, and much of hit testing use logical window dimensions. On HiDPI
displays this can misplace or incorrectly scale drawing and interaction regions.

Establish one coordinate system and apply GLFW content scale explicitly at the
platform boundary.

### Resource ownership needs tightening

- The Linux Skia GPU context is stored as a raw pointer.
- `AppState::fontSystem` is allocated with `new`, is not released, and duplicates
  font-manager ownership already held by `DocumentTypefaceCache`.
- GPU surface/context teardown should use RAII and a documented destruction
  order before the GLFW window and GL context are destroyed.

## Markdown Correctness Findings

### Markdown normalization mutates fenced code — resolved

Status: resolved. The preprocessing pass was removed, md4c now receives the
original source bytes, and nested fenced-code regression coverage was added.

Original finding: `NormalizeMarkdown()` added spaces after heading, quote,
unordered-list, and ordered-list markers on lines it did not recognize as
fenced or indented code. For example, a fenced line containing
`#include <vector>` could become
`# include <vector>`. Similar corruption can affect `*ptr`, `>value`, and text
beginning with an ordered-list-like token.

This changed rendering, syntax highlighting, plain-text copy, and code-block
copy. The implemented resolution removes the normalization and covers the
existing rendering fixture's `#include` case plus a nested fence.

### Inline styles cannot be combined correctly — resolved

Status: resolved. Combinable formatting flags are now independent from link
targets, image sources, content kind, and syntax-highlight roles.

Original finding: `InlineStyle` represented only one style at a time. The model
could not preserve bold plus italic, link plus strong, or other nested
combinations. Entering a nested non-link span also pushed an empty URL, so a
strong span inside a link lost link behavior for the nested portion.

The implemented model splits inline semantics into independent fields:

- combinable formatting flags;
- link target;
- image source and alt text;
- syntax-highlight role.

### Soft line breaks are rendered as hard line breaks — resolved

Status: resolved. Soft and hard breaks remain distinct in the model; layout,
copy/search text, rendering, and hit testing use their intended semantics.

Original finding: both md4c soft and hard break events became `\n`. For
GitHub-style Markdown files, ordinary source line wrapping should not create a
visible line break. The model and layout now preserve that distinction.

### Heading anchors differ from GitHub

- The first duplicate heading receives a `-2` suffix rather than `-1`.
- The current unit test enshrines that mismatch.
- Unicode case folding is incomplete, so anchors containing uppercase non-ASCII
  letters can differ from GitHub.

### Tables and long tokens lack an overflow strategy

Wide tables are compressed into the document width and very long unbroken words
or URLs are allowed to overflow. Add horizontal table scrolling or another
explicit overflow policy, and introduce safe breaking for long tokens.

## Safe Raw HTML Plan

Raw HTML is currently mapped to ordinary paragraph/text nodes, which is why HTML
tags are shown as source. Full browser-compatible HTML is neither necessary nor
appropriate for this native, browser-free viewer.

Implement a native allowlist in phases:

1. Inline semantics: `<br>`, HTML comments, `<sub>`, `<sup>`, `<ins>`, and
   `<kbd>`.
2. Navigation and disclosure: `<a name="...">`, `<details>`, and `<summary>`.
3. Images: `<picture>`, `<source>`, and `<img>` after the local/remote image
   policy and image resource limits are implemented.
4. HTML tables as a separate feature because alignment, `rowspan`, and `colspan`
   require dedicated layout support.

Never execute scripts or event handlers. Ignore style/class attributes and
external embeds. Unknown or unsafe HTML should use a deterministic policy such
as muted source display rather than silently triggering external work.

## Other High-Value GitHub Compatibility Work

After stability work and the inline-model correction:

- footnotes with reference/back-reference navigation;
- GitHub alerts (`NOTE`, `TIP`, `IMPORTANT`, `WARNING`, `CAUTION`);
- front matter recognition;
- `.mdown` and `.mkd` extensions;
- remote images behind an explicit safe policy;
- improved Unicode search and heading normalization.

Repository-context features such as issue references, mentions, and private
remote assets should remain out of scope unless the viewer later gains an
explicit repository context.

## Recommended Implementation Order

1. Completed: add Linux CI and a reproducible sanitizer build mode.
2. Completed: make Linux PDF support explicit and keep non-PDF builds linkable.
3. Exception boundaries are complete; richer phase-aware crash diagnostics remain.
4. Completed: harden image dimensions, decoding, and cache lifetime.
5. File, image, nesting, and code-highlight safeguards are complete; table and
   broader content-complexity thresholds remain.
6. Add minimized crash fixtures and parser/layout fuzz or property tests.
7. Completed: correct Markdown normalization, nested inline semantics, and soft/hard breaks.
8. Implement the safe HTML allowlist.
9. Code-block overflow is complete; footnotes, alerts, front matter, table
   overflow, and long-token handling remain.

## Suggested Linux Crash Capture

Until repository tooling automates this, retain the exact failing document and
all referenced local images, then capture a native backtrace:

```bash
ulimit -c unlimited
gdb --args ./build/mdviewer /absolute/path/to/failing.md
run
bt full
```

On systemd-based distributions, a post-crash core can often be inspected with:

```bash
coredumpctl gdb mdviewer
```

Record whether the failure occurs during initial load, first render, scrolling,
resize, zoom, search, PDF export, or shutdown. That distinction maps directly to
the risk areas above.
