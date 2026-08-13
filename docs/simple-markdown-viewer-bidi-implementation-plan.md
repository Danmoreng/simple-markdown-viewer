# Implementation Plan: Native Complex-Text, BiDi, and RTL Support

> **Repository:** `simple-markdown-viewer`
> **Baseline inspected:** uploaded ZIP `simple-markdown-viewer-current-4X8yZj.zip`
> **Application version in the snapshot:** `0.3.0`
> **Pinned Skia revision:** `508fc9e7f9ad1b6c8b6ed11b260f97e7bfbb363f`
> **Primary target:** correctly render, wrap, select, search, copy, print, and export mixed left-to-right and right-to-left Markdown, including fenced code blocks nested inside lists.

## Agent operating instructions

Implement this plan against the current repository, not against assumptions from an older revision.

1. Read `AGENTS.md` before changing code and preserve its architectural boundaries.
2. Keep the application native, browser-free, read-only, single-window, and shared across Windows and Linux.
3. Do not implement a home-grown Unicode Bidirectional Algorithm and do not use a WebView, Chromium, HTML renderer, or JavaScript text engine.
4. Use the pinned Skia text stack: HarfBuzz through `SkShaper`, Unicode/BiDi and break iteration through `SkUnicode` with the Skia-bundled ICU backend.
5. Keep logical UTF-8 offsets as the source of truth for selection, search, copying, links, and history. Visual reordering must never reorder `DocumentLayout::plainText`.
6. Make the work in small, reviewable commits. Build plumbing, behavior-preserving refactoring, text shaping, interaction changes, and documentation should remain separate where practical.
7. Run the existing test suite after every phase. Do not weaken or delete existing assertions merely to make the feature pass.
8. Where Skia API or output-library names differ from the expectations below, inspect the exact pinned revision with `gn desc` and its headers. Do not silently substitute a primitive shaper.

---

## 1. Current-state findings

The Markdown parser and nested block model are already sufficient for the motivating case.

- `src/markdown/markdown_parser.cpp` maps md4c list items, nested lists, and fenced code blocks into recursive `Block::children`.
- `tests/unit_tests.cpp::MarkdownCorrectnessFoundation()` already proves that an indented fenced code block inside a list survives parsing and preserves source-like lines such as `#include`, `*pointer`, and `>literal`.
- `src/layout/layout_engine.cpp::LayoutBlocks()` recursively lays out nested block children.
- Code blocks already preserve source lines and use horizontal scrolling rather than normal word wrapping.

The missing capability is the text engine.

- `LayoutRuns()` and `FindBreakPoint()` measure text with `SkFont::measureText()` and split at UTF-8 boundaries or ASCII spaces.
- `document_renderer.cpp::DrawTextWithFallback()` draws one Unicode code point at a time from left to right.
- `LineLayout::runs` is currently both logical and visual order; it carries no BiDi level, shaped glyphs, clusters, or caret geometry.
- Selection and search rectangles measure UTF-8 prefixes with `SkFont::measureText()`.
- Windows and Linux hit testing independently scan UTF-8 code points from the left edge of a run.
- List markers, blockquote accents, and nested indentation are always placed on the left.

SkShaper is linked today, but the build explicitly sets `skia_use_icu=false`. At the pinned Skia revision, that means the available SkShaper build can fall back to the primitive implementation and does not provide the complete HarfBuzz + Unicode wrapping pipeline required here.

### Consequence

The parser does **not** need to be replaced. The correct implementation is a shared complex-text layer between the Markdown model and the Skia renderer, followed by geometry-aware rendering and hit testing.

---

## 2. Architectural decision

Use the following stack:

- **HarfBuzz shaping:** `SkShapers::HB::ShaperDrivenWrapper`
- **Unicode/BiDi and break iteration:** `SkUnicodes::ICU::Make()`
- **BiDi iterator:** `SkShapers::unicode::BidiRunIterator(...)`
- **Script segmentation:** `SkShapers::HB::ScriptRunIterator(...)`
- **Font fallback:** a repository-owned `SkShaper::FontRunIterator` that chains Skia's `MakeFontMgrRunIterator()` across semantic/style spans
- **Rendering:** `SkCanvas::drawGlyphs(...)` with shaped glyph IDs, positions, clusters, and run-local UTF-8
- **Logical text:** retain the existing `DocumentLayout::plainText` byte-offset model

Use the ICU-backed shaper deliberately. `ShaperDrivenWrapper` relies on Unicode line and grapheme break iterators and keeps break positions paragraph-relative across Markdown style and soft-break run boundaries. The pinned Skia `ShapeThenWrap` implementation evaluates breaks per segmented run and can consequently split ordinary words after those boundaries. Enabling only Skia's small BiDi subset would require recreating substantial wrapping logic and would increase correctness risk. Binary-size optimization can be evaluated later as a separate change.

Do not call `SkShaper::Make()` and assume success. It may return the primitive shaper when Unicode initialization fails. Construct the ICU and HarfBuzz objects explicitly and expose whether the complex-text path is available.

### Direction rules

Use these rules consistently:

- Normal paragraphs, headings, list-item text, blockquotes, and table cells: resolve the base direction from the first strong character; default to LTR when none exists.
- A soft-wrapped paragraph retains one base direction across all of its visual lines.
- Fenced code blocks: resolve direction independently for each source line, preserving the existing no-wrap/horizontal-scroll behavior.
- Explicit Markdown/allowlisted HTML alignment remains alignment, not text direction. `align="right"` must not be treated as `dir="rtl"`.
- `TextAlign::Default` aligns LTR text to the left and RTL text to the right. Explicit left, center, and right alignment continue to override that default.
- Copying always returns logical source order, never visual order.

---

## 3. Required end state

The implementation is complete when all of the following are true:

- Arabic letters join correctly and combining marks are positioned by HarfBuzz.
- Hebrew and Arabic mixed with English, numbers, punctuation, parentheses, and URLs follow the Unicode Bidirectional Algorithm.
- A fenced code block nested two or more levels inside mixed ordered/unordered lists renders without losing source lines, syntax highlighting, indentation, or horizontal scrolling.
- RTL list items place bullets, numbers, and task checkboxes in the right-side gutter; nested content is inset from the correct side.
- Selection and search highlights can produce multiple visual rectangles for one logical range on a mixed-direction line.
- Clicking either side of an RTL run maps to the correct logical UTF-8 cluster boundary.
- Links remain clickable and selectable after visual reordering.
- `Ctrl+C` returns logical Markdown text in the same order as `DocumentLayout::plainText`.
- Existing image, math, metadata, table, details, search, syntax-highlighting, PDF, and print behavior does not regress.
- The same shared shaped layout is used by interactive rendering, printing, and PDF export.
- Windows and Linux builds, packages, and tests succeed.

---

## 4. Commit sequence

The commit boundaries below are intentional. Use equivalent messages if needed, but preserve the separation of concerns.

1. `test: add bidirectional markdown fixtures and baseline coverage`
2. `build: enable Skia ICU and HarfBuzz complex-text support`
3. `refactor: share the full document font context with layout`
4. `feat: add shaped text layout primitives and caret geometry`
5. `feat: shape and wrap document inline content with SkShaper`
6. `feat: render and hit-test visual bidi runs`
7. `feat: mirror RTL list and block decorations`
8. `test: cover bidi selection search print and export`
9. `docs: document complex-text support and Skia bundle changes`

Each section below describes the expected contents of those commits.

---

## 5. Phase 1 — fixtures and baseline tests

### Files

- Add `test-docs/bidi-complex-text.md`
- Update `tests/unit_tests.cpp`

### Fixture content

The manual fixture must contain, at minimum:

```markdown
# Bidirectional and complex-text fixture

English before العربية بعد English 123 (v2.0).

שלום world 123 — العربية and English.

- عنصر عربي مع `inline_code()` ورابط [English link](https://example.com/path?q=1)
  1. פריט עברי מקונן
     - mixed العربية / English / עברית

        ```cpp
        const char* greeting = "مرحبا";
        // הערה בעברית with value = 123
        auto result = parse(input[2]);
        ```

- [ ] משימה בעברית
- [x] مهمة عربية مكتملة

> اقتباس عربي with English and 42.

| العربية | English | עברית |
| ---: | :--- | ---: |
| قيمة 123 | mixed (v2) | ערך 456 |
```

Also include:

- Arabic combining marks and at least one lam-alef sequence.
- Hebrew with parentheses and punctuation.
- A long RTL code line that requires horizontal scrolling.
- Nested ordered and unordered lists in both directional combinations.
- Inline strong/emphasis/strikethrough and a link inside RTL text.
- One inline math expression and one local image near RTL text to check that atomic inline content does not regress.
- Duplicate RTL headings to confirm heading anchors remain logical and stable.

### Baseline automated tests

Before switching rendering, add assertions that document the parser/model behavior already present:

- The nested fenced code block is found recursively under the list item.
- Its text remains source-exact.
- Syntax role assignment still occurs after parsing/layout.
- `DocumentLayout::plainText` for a mixed-direction fixture remains in logical source order.

Do not add tests that intentionally fail on the baseline branch unless the repository convention supports expected-failure tests. Add the complex-shaping assertions in later commits when the runtime exists.

---

## 6. Phase 2 — Skia ICU/HarfBuzz build and packaging

### 6.1 GN configuration

Update both `build.ps1` and `build.sh` so the GN args explicitly contain:

```text
skia_enable_skshaper=true
skia_enable_skunicode=true
skia_use_harfbuzz=true
skia_use_system_harfbuzz=false
skia_use_icu=true
skia_use_system_icu=false
```

Retain the existing PDF requirement on Linux:

```text
skia_pdf_subset_harfbuzz=false
```

Build these targets explicitly in addition to the existing Skia/SVG targets:

```text
//modules/skshaper:skshaper
//modules/skunicode:skunicode
//third_party/harfbuzz:harfbuzz
//third_party/icu:icu
```

Before hard-coding library paths in CMake, verify outputs from the pinned Skia checkout:

```bash
gn desc out/Static //modules/skshaper:skshaper outputs
gn desc out/Static //modules/skunicode:skunicode_core outputs
gn desc out/Static //modules/skunicode:skunicode_icu outputs
gn desc out/Static //third_party/harfbuzz:harfbuzz outputs
gn desc out/Static //third_party/icu:icu outputs
```

At the inspected revision the expected static artifacts are conceptually:

- `skshaper`
- `skunicode_core`
- `skunicode_icu`
- `harfbuzz`
- `icu`

Use the actual filenames reported by GN for Windows and Linux.

### 6.2 CMake linking

Update both `mdviewer` and `mdviewer_tests` link lists.

Expected Windows order/components:

```text
skshaper.lib
skunicode_icu.lib
skunicode_core.lib
harfbuzz.lib
icu.lib
skia.lib
```

Add `Advapi32.lib`, which is required by the bundled Windows ICU build.

Expected Linux order/components:

```text
libskshaper.a
libskunicode_icu.a
libskunicode_core.a
libharfbuzz.a
libicu.a
libskia.a
```

Keep static-library dependency order correct. If the linker exposes a genuine circular dependency, use a narrowly scoped GNU linker rescan group for the Skia text libraries rather than globally applying `--whole-archive`.

Add `src/text/*.cpp` files introduced later to both the application and test targets.

### 6.3 Windows ICU data

The Skia-bundled ICU backend requires `icudtl.dat` on Windows. The pinned Skia loader searches next to the executable/library.

Add a post-build copy for both targets:

```text
third_party/skia/out/<configuration>/icudtl.dat
    -> $<TARGET_FILE_DIR:mdviewer>/icudtl.dat
    -> $<TARGET_FILE_DIR:mdviewer_tests>/icudtl.dat
```

Fail the build with a clear message when the file is missing. Do not let the application silently ship a primitive text path because the data file was omitted.

Update `.github/workflows/windows-build.yml` so the staged release archive contains `icudtl.dat` beside `mdviewer.exe`.

### 6.4 Windows Skia bundle

Update:

- `.github/workflows/publish-skia-bundle.yml`
- `.github/workflows/windows-build.yml`
- `docs/WINDOWS_SKIA_BUNDLE.md`

The bundle must additionally contain:

```text
third_party/skia/modules/skunicode/include/
third_party/skia/out/Static/skunicode_core.lib
third_party/skia/out/Static/skunicode_icu.lib
third_party/skia/out/Static/harfbuzz.lib
third_party/skia/out/Static/icu.lib
third_party/skia/out/Static/icudtl.dat
```

Make bundle validation check the entire required artifact set, not only `skia.lib`. An old bundle must be rejected and trigger the source-build path instead of reaching CMake and failing at link time.

Do not change `ci/skia-bundle-version.txt` until a new bundle has actually been published and tested. Bundle publication and activation remain separate operations.

### 6.5 Runtime probe

Add a small shared initialization class, for example:

```text
src/text/complex_text_runtime.h
src/text/complex_text_runtime.cpp
```

Its responsibilities:

- Create `SkUnicodes::ICU::Make()`.
- Create `SkShapers::HB::ShaperDrivenWrapper(unicode, fontManager)`.
- Expose `IsAvailable()` and a diagnostic string.
- Never silently report the primitive shaper as complex-text support.

Add a unit test that confirms the ICU and HarfBuzz objects initialize in the packaged test environment.

The viewer may retain the legacy layout as a last-resort runtime fallback so a missing data file does not crash the program. However, official builds and tests must treat unavailable complex-text support as a packaging failure.

### 6.6 Notices

Update `THIRD_PARTY_NOTICES` with explicit notices for the Skia-bundled HarfBuzz and ICU components and their data. Do not assume the top-level Skia notice alone is sufficient for redistributed standalone static libraries and `icudtl.dat`.

---

## 7. Phase 3 — share the complete font context with layout

This should be behavior-preserving and land before the shaping change.

### Problem

`LayoutEngine::ComputeLayout()` currently receives only one `SkTypeface*`, while rendering uses `DocumentTypefaceSet` with regular, bold, heading, code, and the font manager. Complex shaping and fallback require the same complete font context during layout.

### New shared text files

Create:

```text
src/text/document_fonts.h
src/text/document_font_style.h
src/text/document_font_style.cpp
```

Move the platform-neutral `DocumentTypefaceSet` struct into `src/text/document_fonts.h`.

Move or share the following font-style logic from `document_renderer.cpp`:

- heading detection
- selecting regular/bold/heading/code typefaces
- font size for block and inline styles
- keyboard, subscript, and superscript scaling
- emphasis skew and strong styling
- subpixel/hinting setup where appropriate

The renderer can still own color decisions.

`render/document_typefaces.*` remains responsible for creating and caching the typefaces; it should include the new shared header.

### Signature changes

Change `LayoutEngine::ComputeLayout()` to receive the full typeface set:

```cpp
static DocumentLayout ComputeLayout(
    const DocumentModel& doc,
    float width,
    const DocumentTypefaceSet& typefaces,
    float baseFontSize,
    ImageSizeProvider imageSizeProvider = nullptr,
    LayoutOptions options = {});
```

Propagate this through:

- `ViewerController::OpenFile()`
- `ViewerController::ReloadCurrentFile()`
- `ViewerController::Relayout()`
- Windows host call sites
- Linux host call sites
- PDF export
- print preparation
- unit tests

Remove the redundant `layoutTypeface` fields from `PdfExportRequest` and `PrintDocumentRequest`; their `DocumentTypefaceSet` becomes the single font source of truth.

### Acceptance for this refactor

- All existing tests pass before complex shaping is enabled in layout.
- Existing LTR geometry tests remain within their current tolerances.
- PDF and print tests still use the same fonts as interactive rendering.

---

## 8. Phase 4 — shaped layout model and shared geometry

### 8.1 Extend the layout model

Add a resolved direction enum in the shared text/layout layer:

```cpp
enum class ResolvedTextDirection {
    LeftToRight,
    RightToLeft,
};
```

Extend `LineLayout` with fields similar to:

```cpp
float width = 0.0f;
ResolvedTextDirection direction = ResolvedTextDirection::LeftToRight;
```

Extend `BlockLayout` with a resolved flow direction used by decorations and list gutters.

Extend `RunLayout` so shaped text no longer depends on renderer-side remeasurement:

```cpp
struct CaretStop {
    size_t textPosition = 0; // global logical plainText byte offset
    float x = 0.0f;         // line-local visual x
};

struct RunLayout {
    // Existing semantic fields remain.
    float visualX = 0.0f;       // line-local position
    float visualWidth = 0.0f;
    uint8_t bidiLevel = 0;
    size_t semanticSpanId = 0;
    bool shaped = false;
    float baselineShift = 0.0f;

    SkFont shapedFont;
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> glyphPositions;
    std::vector<uint32_t> glyphClusters; // normalized to run.text
    std::vector<CaretStop> caretStops;
};
```

The exact ownership may differ, but preserve these invariants:

- Every visual run knows its visual x and width.
- Every shaped text run carries the font used for those glyphs.
- Clusters map back to logical UTF-8 positions.
- Caret stops occur only at safe cluster/grapheme boundaries.
- Image and math runs remain atomic.

### 8.2 Shared geometry helpers

Create a shared module, for example:

```text
src/text/text_geometry.h
src/text/text_geometry.cpp
```

Provide helpers equivalent to:

```cpp
size_t HitTestTextRun(const RunLayout& run, float xInLine);
std::vector<SkRect> GetTextRangeRects(
    const LineLayout& line,
    size_t logicalStart,
    size_t logicalEnd,
    float top,
    float bottom,
    float horizontalPadding = 0.0f);
```

These helpers must:

- work for both LTR and RTL runs;
- snap through ligatures and combining sequences at cluster boundaries;
- return multiple rectangles when a logical range is visually discontiguous;
- handle zero-length image placeholders and atomic math runs safely;
- avoid platform-specific font measurement.

### 8.3 Legacy compatibility

During migration, fill `visualX`, `visualWidth`, and simple caret stops for legacy LTR runs as well. This allows the renderer and hit tester to migrate before every specialized block uses the new shaper.

---

## 9. Phase 5 — complex-text shaping subsystem

Create a focused subsystem rather than putting all SkShaper code directly into `layout_engine.cpp`.

Suggested files:

```text
src/text/shaped_text_layout.h
src/text/shaped_text_layout.cpp
```

### 9.1 Input spans

Flatten Markdown `InlineRun`s into semantic shaping spans. A span should retain:

- shaping UTF-8 range;
- global logical `plainText` start and length;
- `InlineFormatting`;
- `SyntaxRole`;
- `MetadataRunRole` when relevant;
- link target;
- original inline kind;
- math/image object size and layout data when atomic;
- a stable `semanticSpanId` so decorations can group fallback/script fragments belonging to the same Markdown span.

Normal text maps one-to-one between shaping bytes and logical bytes.

- `SoftBreak` becomes a normal space in both shaping text and logical text.
- `HardBreak` terminates the current shaping paragraph/source line and appends the same logical newline as today.
- Images remain zero-length in logical copy text, matching current behavior.
- Math remains atomic visually but maps to its existing logical source text range.

### 9.2 Atomic objects

Represent inline images and inline math in the shaping buffer with a neutral placeholder span so BiDi ordering and line wrapping can account for their width.

A practical implementation is:

- use one EM SPACE (`U+2003`) as internal shaping text;
- assign that object span a trivial `SkFont` whose horizontal scale makes the shaped advance equal to the object's exact width;
- map the placeholder shaping range back to the object's logical range;
- discard the placeholder glyph at render time and emit the existing image or math `RunLayout` instead;
- include object height when computing the final line ascent/descent/height.

Do not expose the placeholder in `plainText`, selection text, search text, or copied text.

Display math should retain its current dedicated centered-line behavior rather than being embedded as an inline placeholder.

The metadata bar has specialized separator/tag geometry. It may retain its existing layout path in the first complex-text PR, provided there is no regression and the limitation is documented. Do not let that specialized block delay document-body BiDi support.

### 9.3 Base-direction detection

Implement a shared first-strong resolver using the already-fetched `utf8proc` data:

- first `L` character -> LTR;
- first `R` or `AL` character -> RTL;
- no strong character -> LTR.

Do not infer direction from punctuation, digits, alignment, font, locale, or filename.

### 9.4 Style-aware fallback iterator

Implement a repository-owned `SkShaper::FontRunIterator` that chains inner iterators.

For each semantic text span:

1. Configure an `SkFont` from `DocumentTypefaceSet`, block type, formatting, and current font scale.
2. Create `SkShaper::MakeFontMgrRunIterator()` for that span to obtain system fallback.
3. Expose the inner iterator's end offsets in the full paragraph's byte coordinate space.
4. Move to the next semantic span after the inner iterator is exhausted.

For object-placeholder spans, return a trivial font run with the calibrated placeholder font.

This lets style boundaries, syntax highlighting, and font fallback coexist without shaping one Unicode code point at a time.

### 9.5 Shaper invocation

For a normal paragraph-like block:

```cpp
auto unicode = SkUnicodes::ICU::Make();
auto shaper = SkShapers::HB::ShaperDrivenWrapper(unicode, sk_ref_sp(typefaces.fontMgr));
auto bidi = SkShapers::unicode::BidiRunIterator(
    unicode,
    utf8.data(),
    utf8.size(),
    baseDirection == RTL ? 1 : 0);
auto script = SkShapers::HB::ScriptRunIterator(utf8.data(), utf8.size());
auto language = SkShaper::MakeStdLanguageRunIterator(utf8.data(), utf8.size());

shaper->shape(
    utf8.data(),
    utf8.size(),
    fontRuns,
    *bidi,
    *script,
    *language,
    wrapWidth,
    &handler);
```

Adapt names to the exact pinned headers. Fail cleanly if any component is unavailable.

For code blocks:

- split by preserved source newline;
- resolve base direction per source line;
- shape with an effectively unbounded width so one source line remains one visual line;
- record the shaped natural width for the existing horizontal-scroll range;
- continue to apply the print/PDF fit scale only through `LayoutOptions::fitHorizontalOverflow`.

### 9.6 Custom RunHandler

Implement a custom `SkShaper::RunHandler` based on the lifecycle used by Skia's `SkTextBlobBuilderRunHandler`:

- `beginLine()` resets line metrics and temporary runs.
- `runInfo()` collects font metrics and looks up semantic/object information for the UTF-8 range.
- `commitRunInfo()` computes the baseline and line height, including image/math object height.
- `runBuffer()` allocates glyph IDs, cumulative advance positions, offsets, and clusters. Prefer supplying an offsets array so cumulative advances are available for caret construction.
- `commitRunBuffer()` converts the shaped buffer into one or more visual `RunLayout`s, normalizes clusters, assigns logical ranges, and builds caret stops.
- `commitLine()` finalizes `LineLayout::width`, direction, y, height, logical range, and visual x alignment.

The handler receives runs in visual order after `ShaperDrivenWrapper` performs line wrapping and BiDi reordering. Keep their logical UTF-8 ranges for copying and interaction.

### 9.7 Caret construction

Derive caret stops from HarfBuzz clusters, not individual Unicode code points.

- Always include logical start and logical end boundaries.
- For LTR runs, logical start generally maps to the left visual boundary.
- For RTL runs, logical start generally maps to the right visual boundary.
- Multiple characters sharing a ligature cluster must not create fake interior caret positions.
- Combining marks must stay attached to their base cluster.
- Sort or query caret stops in visual-x order for hit testing while retaining logical offsets.

Do not assert exact glyph IDs in tests; they vary with installed fallback fonts.

---

## 10. Phase 6 — integrate shaping into `LayoutEngine`

### 10.1 Replace normal text layout

Refactor `LayoutRuns()` so paragraph-like content delegates to `shaped_text_layout` instead of incrementally calling `SkFont::measureText()`.

Use the shaped path for:

- paragraphs;
- headings;
- list-item text;
- blockquotes and GitHub alerts;
- table headers and cells;
- code blocks and syntax-highlighted code;
- inline emphasis, strong, strikethrough, links, inline code, keyboard text, subscript, and superscript.

Keep existing dedicated paths for:

- thematic breaks;
- block/table containers;
- display math;
- details container structure;
- metadata bar if deferred as noted above.

### 10.2 Natural width measurement

Replace `MeasureInlineRunsWidth()` and table preferred-width measurement with a no-wrap shaped measurement using the same fonts and fallback as final layout. This prevents table and code widths from being computed with unshaped Arabic metrics.

### 10.3 Plain-text invariants

Preserve the current behavior exactly:

- text bytes append in logical source order;
- soft breaks copy/search as a space;
- hard breaks copy/search as `\n`;
- math copies/searches as its source-preserving text;
- images do not insert hidden replacement characters;
- table TSV/CSV serialization remains unchanged.

Add direct assertions around these invariants.

### 10.4 Direction-aware alignment

Replace `ResolveLineX()` behavior for `TextAlign::Default`:

```text
Default + LTR -> content left
Default + RTL -> content right minus line width
Left          -> content left
Center        -> centered
Right         -> content right minus line width
```

Clamp overflow in the same manner as the current implementation so a line wider than its viewport remains reachable by horizontal scrolling.

### 10.5 Direction-aware nested insets

Replace the single scalar recursion indent with directional insets, for example:

```cpp
struct FlowInsets {
    float left = 0.0f;
    float right = 0.0f;
};
```

For each list item, resolve its first visible textual descendant's direction before assigning its item gutter.

- LTR list item: add item indentation to the left inset.
- RTL list item: add item indentation to the right inset.
- Nested lists inherit the containing item's flow side unless their own item text resolves differently.

Preserve current LTR x positions within existing test tolerance. Add explicit tests for mirrored RTL positions.

---

## 11. Phase 7 — renderer migration

### 11.1 Draw shaped text

Update `document_renderer.cpp::DrawLine()`:

- use `run.visualX` instead of accumulating logical run widths;
- for shaped text runs, call `SkCanvas::drawGlyphs()` with stored glyphs, positions, clusters, run-local UTF-8, `SkFont`, and the existing color paint;
- retain existing image and math drawing;
- retain a legacy draw path only for specialized/unmigrated runs and runtime fallback.

After the shaped path is stable, remove `DecodeUtf8Codepoint()`, `GetFallbackTypefaceForCodepoint()`, and code-point-at-a-time `DrawTextWithFallback()` from the document-body rendering path. A separate simple fallback may remain for app chrome until that UI is migrated.

### 11.2 Decorations

Update inline decoration drawing to use visual fragments:

- inline-code and keyboard backgrounds;
- link underlines;
- strikethrough;
- selection fill;
- search fill and current-match stroke.

A semantic Markdown span may split into multiple visual/font/script runs. Use `semanticSpanId` to join adjacent visual fragments when appropriate, but do not draw one rectangle across unrelated intervening RTL content.

### 11.3 Selection and search

Replace prefix-width calculations in:

- `DrawSelectionForLine()`
- `DrawSearchForLine()`
- `DrawSearchStrokeForLine()`

with `GetTextRangeRects()`.

One logical match or selection may produce more than one rectangle on a single line. Draw every returned rectangle.

### 11.4 Images and math

When the shaper emits an object placeholder, render the corresponding existing image/math object at the placeholder's visual x. Do not draw the internal placeholder glyph.

### 11.5 Print and PDF

No second text layout implementation is permitted. `PreparePrintDocument()` must invoke the same shaped `LayoutEngine`, and `RenderPrintDocumentPage()` must use the same glyph renderer. Verify that PDF output contains vector text/glyph drawing and that RTL geometry matches the screen.

---

## 12. Phase 8 — hit testing and interaction

### 12.1 Remove platform text measurement

Refactor `HitTestDocument()` to use layout-owned visual geometry directly.

Remove these platform callbacks once migration is complete:

- `get_run_visual_width`
- `find_text_position_in_run`

Retain only information genuinely external to layout, such as the current per-block horizontal scroll offset, or pass that offset into hit testing separately.

Delete the duplicate UTF-8 scanning/measurement implementations from:

- `src/platform/win/win_interaction.cpp`
- `src/platform/linux/linux_interaction.cpp`

Both platforms must call the same shared `HitTestTextRun()` helper.

### 12.2 RTL hit behavior

For an RTL visual run:

- a click near the visual right edge maps near the logical start;
- a click near the visual left edge maps near the logical end;
- clicks inside a ligature snap to the nearest safe cluster boundary;
- link/image/math semantics remain attached to the visually hit fragment.

### 12.3 Selection behavior

Mouse-drag selection continues to store logical anchor/focus offsets. Crossing visual direction boundaries may make the highlighted shape non-contiguous; this is expected. Copying the selection must use the logical substring of `plainText` exactly as before.

### 12.4 Search behavior

Search matching remains logical. The existing ASCII-only case folding is a separate backlog item and should not be mixed into this change. Hebrew/Arabic exact-text searches must highlight the correct visual glyphs.

---

## 13. Phase 9 — direction-aware block decorations

### Lists

Update `DrawBlockDecoration()` for `ListItem`:

- LTR bullet/number/task checkbox: existing left gutter.
- RTL bullet/number/task checkbox: mirrored right gutter.
- Ordered marker text should be positioned so the marker's own visual width remains outside the content bounds.
- Marker baseline must still come from the first visible descendant line.

### Blockquotes and alerts

Mirror the blockquote accent to the right for RTL blocks. For GitHub alert title/icon rows, place the icon/title from the right edge when the block direction is RTL.

### Details

At minimum, shape the summary text correctly. Mirroring the chevron and summary padding is recommended in this change if it can be done without destabilizing details hit testing; otherwise record it as an explicit follow-up.

### Tables

Cell text direction resolves independently per cell. Existing Markdown column alignment remains authoritative when explicitly provided.

### Code blocks

The code-copy button and language badge may remain in their established top-right positions. They are controls, not paragraph content. Ensure they do not overlap right-aligned RTL source lines; reserve the same control area or clip as today.

---

## 14. Automated test matrix

Add focused tests in `tests/unit_tests.cpp`. Prefer structural and geometry invariants over pixel-perfect cross-platform goldens.

### Runtime/build tests

- ICU backend initializes.
- HarfBuzz `ShaperDrivenWrapper` initializes.
- Windows test executable can find `icudtl.dat`.
- The complex-text path reports available in official test builds.

### Direction and shaping tests

- First-strong Arabic resolves RTL.
- First-strong Hebrew resolves RTL.
- First-strong English resolves LTR.
- Digits/punctuation only default to LTR.
- Mixed LTR/RTL line contains both even and odd BiDi levels.
- RTL line's logical start caret is visually to the right of its logical end caret.
- Arabic output has nonempty shaped glyph runs and nonzero advances.
- Combining marks do not introduce unsafe independent caret stops.
- A ligature cluster does not expose a caret between bytes that share the same HarfBuzz cluster.

Do not assert exact font family, glyph ID, or total width across operating systems.

### Wrapping tests

- Mixed-direction paragraph wraps without exceeding the requested width.
- Each visual line retains the paragraph base direction.
- Long unbroken complex-script text breaks only at a safe grapheme/cluster boundary.
- Code source lines remain unwrapped.
- RTL code line horizontal content width is recorded correctly.

### Nested-list tests

- Fenced code nested under mixed ordered/unordered lists remains present.
- LTR item bounds match the prior layout within tolerance.
- RTL item content is inset from the right.
- RTL marker lies to the right of the item content bounds.
- A nested LTR child under an RTL parent and a nested RTL child under an LTR parent both remain inside the viewport.

### Interaction tests

- Hit right edge of an RTL run -> logical start.
- Hit left edge -> logical end.
- Hit a visual link fragment -> correct link target.
- Select a Hebrew/English mixed range -> multiple valid rectangles when required.
- Copy selected text -> exact logical substring.
- Search an exact Arabic/Hebrew term -> highlight rectangles overlap its shaped run.

### Existing-feature regression tests

- Syntax roles remain attached to the correct text after shaping.
- Inline code, strong, emphasis, strikethrough, subscript, superscript, and keyboard formatting survive run splitting.
- Image hit testing still distinguishes inside vs. beside the image.
- Math remains atomic for hit testing and source-preserving for copy/search.
- Table TSV/CSV remains unchanged.
- Details summary toggle remains clickable.
- Horizontal code/table scrolling remains block-local.

### Rendering/output tests

Create raster surfaces as existing tests do and assert rendering completes for:

- Arabic paragraph;
- Hebrew/English mixed paragraph;
- RTL nested list with code;
- RTL selection and search overlays.

Prepare a print document containing mixed-direction text and assert:

- shaped runs survive into the print layout;
- page ranges are valid;
- page rendering completes.

Where PDF support is enabled, add a small export smoke test that checks a nonempty PDF is produced from the complex-text fixture.

---

## 15. Manual validation matrix

Run the fixture on both Windows and Linux with light, sepia, and dark themes.

### Document rendering

- Arabic joining and diacritics look natural.
- Hebrew punctuation and parentheses appear on the expected visual side.
- English words and numbers embedded in RTL sentences remain readable.
- RTL headings are right-aligned by default and retain working outline anchors.
- Inline links are underlined only beneath their visual fragments.
- Inline code remains visually isolated and readable inside RTL prose.

### Lists and code

- Bullets/numbers/task boxes appear in the correct gutter.
- Nested list indentation mirrors correctly.
- Nested fenced code keeps every source line.
- Syntax highlighting colors remain aligned with glyphs.
- Copy button copies the complete logical source.
- Long lines scroll horizontally without reversing the scrollbar or clipping controls.

### Interaction

- Drag selection both left-to-right and right-to-left across mixed text.
- Copy and paste into a plain-text editor; verify logical sentence order.
- Click mixed-direction links at both ends of the visual link.
- Search Arabic and Hebrew terms and cycle next/previous.
- Zoom, resize, outline resize, reload, and back/forward do not corrupt selection geometry.

### Output

- Print through the native dialog.
- Export to PDF and inspect Arabic joining, Hebrew order, code blocks, tables, and page breaks.
- Verify the Windows release works after moving the extracted directory to a clean location and that `icudtl.dat` is present beside `mdviewer.exe`.

### Linux environments

Perform the existing Linux smoke-test checklist on at least one X11 and one Wayland session if available. Verify the new static ICU/HarfBuzz linkage does not introduce build-tree runtime dependencies.

---

## 16. Documentation and product updates

Update:

- `README.md`
- `DEVELOPMENT_PLAN.md`
- `docs/PRODUCT_VISION.md`
- `docs/WINDOWS_SKIA_BUNDLE.md`
- `THIRD_PARTY_NOTICES`

README feature language should state that the document surface supports native HarfBuzz shaping and Unicode BiDi for Arabic, Hebrew, mixed-direction text, and complex scripts on Windows and Linux.

Remove or revise the product-vision note that RTL scripts currently have shaping/BiDi limitations.

Add a completed development-plan item for complex-text support, including rendering, selection, search, copy, lists, code blocks, print, and PDF.

Document any deliberately deferred limitation, such as RTL shaping in custom app chrome or the metadata bar. Do not claim those areas are complete if they still use `drawString()`/code-point fallback.

---

## 17. Performance and safety guardrails

- Create one ICU/SkShaper runtime per layout operation, not per glyph or code point.
- Do not shape inside the paint loop. Rendering should consume cached glyph geometry from `DocumentLayout`.
- Preserve the existing syntax-highlight cache.
- Avoid O(n²) prefix measurement; the HarfBuzz/ICU shaper should own line breaking.
- Put a reasonable upper bound on one shaping paragraph/source line so a malicious single line cannot request unbounded temporary arrays. Fall back visibly or chunk only at safe boundaries when the bound is exceeded.
- Validate all byte ranges before indexing the shaping-to-logical mapping.
- Use `size_t` for UTF-8 byte offsets and check conversions to Skia/ICU 32-bit positions.
- Do not use raw pointers into temporary paragraph strings after layout returns.
- Keep `SkFont`, typeface references, glyph arrays, and run text alive for as long as `DocumentLayout` is rendered.
- Run AddressSanitizer and UndefinedBehaviorSanitizer tests on Linux after the final integration.

---

## 18. Known risks and mitigations

### Windows package starts but complex text is broken

Likely cause: missing `icudtl.dat` or stale Skia bundle. Mitigation: strict build/bundle validation, explicit runtime probe, post-build copy, release archive assertion.

### Link errors after enabling ICU/HarfBuzz

Likely cause: omitted transitive static libraries or wrong link order. Mitigation: use `gn desc ... outputs` on the pinned revision; link `skshaper`, both skunicode components, HarfBuzz, ICU, and Skia in dependency order.

### Correct display but broken selection

Likely cause: using character/code-point positions instead of HarfBuzz clusters. Mitigation: make caret stops and range rectangles part of the initial shaped layout model, not a later heuristic.

### Arabic joins break at formatting boundaries

HarfBuzz needs context. The pinned implementation passes pre- and post-context around segmented runs, which helps preserve shaping context. Keep semantic spans in one paragraph shaping invocation and use iterators rather than calling the shaper independently for every Markdown run.

### Image/math placement shifts wrapping

Likely cause: placeholder advance differs from object width. Mitigation: calibrate the placeholder font advance and assert it is within a small tolerance before using it; include object height in line metrics.

### Old LTR documents shift unexpectedly

Likely cause: layout previously measured with one typeface while rendering used another. Mitigation: land the full-font-context refactor separately, add LTR geometry tests, and use the same configured fonts for measurement and rendering.

### Memory increase on large documents

Shaped glyph and cluster arrays add layout memory. Mitigation: store only required geometry, avoid duplicate full-paragraph strings per run, and measure with a large fixture before release. Do not prematurely add a second cache until profiling identifies a need.

---

## 19. Final build commands

### Windows

```powershell
# Rebuild the pinned Skia text stack.
.\build.ps1 -Clean -Configuration Release -SkiaOnly

# Build application and tests against it.
.\build.ps1 -SkipSkia -Configuration Release
.\build.ps1 -SkipSkia -Configuration Release -Target mdviewer_tests

ctest --test-dir build -C Release --output-on-failure

# GUI startup and manual fixture.
.\build.ps1 -SkipSkia -Configuration Release -RunSmokeTest
.\build\Release\mdviewer.exe .\test-docs\bidi-complex-text.md
```

Confirm these files exist:

```text
build/Release/mdviewer.exe
build/Release/icudtl.dat
build/Release/res/.clatexmath-res_root
build/Release/mdviewer_tests.exe
```

### Linux

```bash
./build.sh
cmake --build build --target mdviewer_tests --parallel 2
ctest --test-dir build --output-on-failure
./build/mdviewer test-docs/bidi-complex-text.md
./package-linux.sh --skip-build
```

Run the existing sanitizer configuration from `.github/workflows/linux-build.yml` and verify the packaged archive with `ldd` as the workflow already does.

---

## 20. Definition of done checklist

- [x] Existing parser regression proves nested fenced code is retained.
- [x] ICU and HarfBuzz are enabled in pinned Skia builds.
- [x] Windows ships `icudtl.dat` and rejects stale bundles.
- [x] Full `DocumentTypefaceSet` is shared with layout.
- [ ] Paragraph text is shaped and wrapped by SkShaper/SkUnicode.
- [ ] Code lines are shaped without source-line wrapping.
- [ ] Visual runs carry glyphs, clusters, direction, x positions, and caret stops.
- [ ] Renderer uses `drawGlyphs()` for document text.
- [ ] Selection/search geometry supports discontiguous visual rectangles.
- [ ] Windows and Linux hit testing use shared cluster geometry.
- [ ] RTL list markers and insets are mirrored.
- [ ] Blockquote/alert decoration follows block direction.
- [ ] Copy remains logical and source-preserving.
- [ ] Links, inline code, syntax roles, images, math, tables, and details do not regress.
- [ ] Print and PDF use the same shaped layout.
- [ ] Unit, sanitizer, package, and manual fixture checks pass.
- [ ] README, development plan, product vision, bundle docs, and notices are updated.
- [ ] A new Windows Skia bundle is published and activated only after validation.

---

## 21. Recommended follow-ups, not blockers for the core feature

Keep these out of the core PR unless the implementation naturally makes them trivial:

- Shape RTL text in the custom menu bar, search overlay, hover URL overlay, and other app chrome.
- Cache shaped outline/sidebar labels by text, font, zoom, and width.
- Add explicit safe HTML `dir="ltr|rtl|auto"` support after the automatic direction path is stable.
- Improve Unicode-aware case-insensitive search; the current ASCII lowercase behavior is a separate milestone.
- Evaluate replacing full ICU with a smaller Skia Unicode backend only after correctness, package size, and line/grapheme break coverage have been measured.
- Add keyboard caret navigation if the product later introduces editable/caret-like document navigation.

The core PR should prioritize correct document rendering and interaction over premature dependency-size optimization.
