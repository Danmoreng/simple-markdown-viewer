\# Development Plan: Cross-Platform Read-Only Markdown Viewer



\## Architecture: C++ + Skia + Native Platform Layer



\## Goal



Build a \*\*fast, lightweight, beautiful, read-only Markdown viewer\*\* with a \*\*custom-rendered document surface\*\*.



The application should:



\* open Markdown files and render them beautifully

\* support \*\*scrolling\*\*

\* support \*\*text selection + copy\*\*

\* be \*\*read-only\*\*

\* have a \*\*single-window UI\*\*

\* use a \*\*custom-drawn document view\*\*

\* use \*\*native platform windows and event loops\*\*

\* use \*\*Skia\*\* for rendering

\* be designed for \*\*cross-platform support\*\*

\* start with \*\*Windows\*\*, but keep the architecture ready for \*\*macOS and Linux\*\*



This is \*\*not\*\* a general UI toolkit project.

It is a \*\*narrow, document-viewer-focused application architecture\*\*.



\---



\# Product Scope



\## In scope for v1



\* Open a Markdown file from:



&#x20; \* command line argument

&#x20; \* native file open dialog

&#x20; \* drag and drop

\* Render Markdown as a styled document

\* Support:



&#x20; \* paragraphs

&#x20; \* headings

&#x20; \* emphasis / strong

&#x20; \* inline code

&#x20; \* fenced code blocks

&#x20; \* blockquotes

&#x20; \* unordered and ordered lists

&#x20; \* horizontal rules

&#x20; \* links as styled text

\* Smooth vertical scrolling

\* Mouse-based text selection

\* Copy selected text to clipboard

\* Beautiful typography and spacing

\* Dark and light theme support

\* Single-window display

\* Fast startup

\* Small binary footprint relative to browser-based stacks

\* Platform-specific shell layer with shared rendering/document engine



\## Out of scope for v1



\* Editing Markdown

\* Split source/preview mode

\* Live reload

\* Embedded browser engine

\* Rich interactions beyond:



&#x20; \* scrolling

&#x20; \* text selection

&#x20; \* copy

\* Image support unless easy to add

\* Table support unless easy to add

\* Search

\* Annotation

\* Tabs / multi-document UI

\* Settings dialog

\* Accessibility beyond basic keyboard compatibility unless specifically planned later



\---



\# High-Level Architecture



The system should be divided into four major layers:



\## 1. Native Platform Layer



Responsible for:



\* creating the application window

\* receiving OS events

\* mouse / keyboard input

\* clipboard integration

\* file dialogs

\* drag-and-drop

\* timers / invalidation / redraw scheduling

\* DPI scaling / window resize

\* OS integration



This layer should be \*\*thin\*\* and platform-specific.



\### Initial target



\* Windows first



\### Planned future targets



\* macOS

\* Linux



The platform layer should not contain Markdown logic or rendering decisions.



\---



\## 2. Shared Rendering Layer



Responsible for:



\* drawing text

\* drawing backgrounds

\* drawing selection highlights

\* drawing block decorations

\* clipping

\* scroll offset application

\* theme colors

\* viewport drawing



This layer should be implemented on top of \*\*Skia\*\*.



Skia is the rendering backend, not the app framework.



\---



\## 3. Shared Document Engine



Responsible for:



\* reading Markdown files

\* parsing Markdown into an AST or block model

\* converting parsed Markdown into a styled layout tree

\* line breaking and text layout

\* block spacing

\* text run generation

\* document metrics

\* selection mapping

\* hit testing

\* extracting plain text for clipboard copy



This is the core of the application.



\---



\## 4. Application Controller Layer



Responsible for:



\* opening files

\* reloading current file

\* theme switching if included

\* wiring platform events to document view behavior

\* maintaining app state such as:



&#x20; \* current file path

&#x20; \* current scroll position

&#x20; \* current selection state

&#x20; \* current document layout

&#x20; \* current theme



\---



\# Design Philosophy



This app should behave like a \*\*high-quality document reader\*\*, not like a generic desktop GUI app.



Design priorities:



1\. \*\*fast startup\*\*

2\. \*\*beautiful text rendering\*\*

3\. \*\*clean layout and spacing\*\*

4\. \*\*small and maintainable architecture\*\*

5\. \*\*cross-platform readiness\*\*

6\. \*\*limited interaction surface\*\*

7\. \*\*no unnecessary controls or framework complexity\*\*



Avoid building a reusable general-purpose UI framework.



Instead, build a \*\*minimal document-surface architecture\*\*:



\* one window

\* one scrollable rendered document

\* minimal chrome

\* minimal commands



\---



\# Recommended Source Layout



```text

/CMakeLists.txt

/build.ps1

/README.md



/third\_party/

/assets/



/src/main.cpp



/src/app/

&#x20;   app.h

&#x20;   app.cpp

&#x20;   app\_state.h



/src/platform/

&#x20;   platform\_window.h

&#x20;   platform\_events.h

&#x20;   clipboard.h

&#x20;   file\_dialog.h

&#x20;   drag\_drop.h

&#x20;   dpi.h



/src/platform/win/

&#x20;   win\_main.cpp

&#x20;   win\_window.h

&#x20;   win\_window.cpp

&#x20;   win\_clipboard.cpp

&#x20;   win\_file\_dialog.cpp

&#x20;   win\_drag\_drop.cpp

&#x20;   win\_dpi.cpp

&#x20;   win\_skia\_surface.cpp



/src/render/

&#x20;   renderer.h

&#x20;   renderer.cpp

&#x20;   theme.h

&#x20;   colors.h

&#x20;   typography.h

&#x20;   draw\_context.h

&#x20;   paint\_utils.cpp

&#x20;   selection\_paint.cpp



/src/markdown/

&#x20;   markdown\_loader.h

&#x20;   markdown\_loader.cpp

&#x20;   markdown\_ast.h

&#x20;   markdown\_parser.h

&#x20;   markdown\_parser.cpp



/src/layout/

&#x20;   document\_model.h

&#x20;   document\_model.cpp

&#x20;   document\_layout.h

&#x20;   document\_layout.cpp

&#x20;   text\_layout.h

&#x20;   text\_layout.cpp

&#x20;   line\_breaking.cpp

&#x20;   block\_layout.cpp



/src/view/

&#x20;   document\_view.h

&#x20;   document\_view.cpp

&#x20;   hit\_test.h

&#x20;   hit\_test.cpp

&#x20;   selection\_model.h

&#x20;   selection\_model.cpp

&#x20;   scrolling\_model.h

&#x20;   scrolling\_model.cpp



/src/util/

&#x20;   utf8.h

&#x20;   utf8.cpp

&#x20;   file\_io.h

&#x20;   file\_io.cpp

&#x20;   string\_utils.h

&#x20;   geometry.h

&#x20;   result.h

&#x20;   log.h



/resources/

&#x20;   app.rc

&#x20;   manifest.xml

```



This can be simplified, but the separation of concerns should remain.



\---



\# Core Technical Decisions



\## 1. Language



Use \*\*C++20\*\* if practical, otherwise \*\*C++17\*\*.



Preferred style:



\* RAII

\* explicit ownership

\* minimal heap churn in hot paths

\* modern standard library containers and utilities

\* narrow interfaces between layers



\---



\## 2. Rendering Backend



Use \*\*Skia\*\* as the 2D renderer.



Skia is responsible for:



\* text drawing

\* shape drawing

\* clipping

\* antialiasing

\* color fills

\* selection highlight rendering



Do not use HTML/CSS/web rendering.



\---



\## 3. Markdown Parsing



Use one of these approaches:



\### Option A: Minimal internal parser



Good if you want tight scope and full control.



\### Option B: Small third-party Markdown parser library



Recommended if:



\* the library is lightweight

\* it has a permissive license

\* it gives stable parsing

\* it does not force HTML rendering



Preferred approach for v1:



\* use a lightweight Markdown parser to produce an AST or event stream

\* convert that into your own internal document model



Do \*\*not\*\* tie the architecture to HTML generation.



The Markdown parser should be treated as a syntax front-end, not the renderer.



\---



\## 4. Text Layout



This is the most important subsystem.



The document renderer must support:



\* font selection

\* block spacing

\* line wrapping

\* inline style runs

\* code span styling

\* selection range painting

\* mapping between screen coordinates and text positions



Text layout should be implemented in a reusable but narrow way:



\* enough for a Markdown document renderer

\* not a full rich-text editor engine



\---



\# Document Pipeline



The rendering pipeline should be:



\## Step 1: Load file



\* read UTF-8 Markdown file

\* detect BOM if necessary

\* store original source text



\## Step 2: Parse Markdown



\* generate AST or block/inline parse tree



\## Step 3: Build internal document model



Convert Markdown structures into a document model containing:



\* block nodes

\* inline runs

\* style spans

\* plain text mapping info

\* source range mapping if useful



\## Step 4: Layout



Compute layout for the current viewport width:



\* line breaks

\* block positions

\* text runs

\* code block rectangles

\* quote bars

\* list indentation

\* total document height



\## Step 5: Render



Render the visible portion of the layout tree using Skia:



\* background

\* blocks

\* text

\* selections

\* decorations



\## Step 6: Hit testing



Map mouse coordinates to:



\* text positions

\* selectable text offsets

\* line ranges



\## Step 7: Clipboard extraction



Convert selected logical text range into plain text and place on clipboard.



\---



\# Visual Design Direction



This app should feel like a \*\*beautiful Markdown reader\*\*, not a generic developer utility.



\## Design goals



\* generous whitespace

\* excellent typography

\* calm color palette

\* subtle block styling

\* smooth text selection

\* clear hierarchy between headings, body text, code, and quotes

\* dark mode and light mode

\* minimal chrome



\## Rendering style suggestions



\* centered reading column with max width

\* body text optimized for readability

\* distinct heading scale

\* code blocks with subtle background and padding

\* blockquotes with a vertical accent rule

\* lists with careful indentation

\* soft selection color

\* optional document top padding and bottom padding



\## Important note



Do not attempt flashy UI or animation-heavy behavior in v1.

The beauty should come from:



\* typography

\* spacing

\* rhythm

\* rendering quality



\---



\# Interaction Model



\## Supported interactions



Only these interactions should exist in v1:



\* scroll with mouse wheel / touchpad / scrollbar

\* click and drag to select text

\* copy selected text

\* open file

\* drag and drop file



\## Unsupported interactions



Do not add:



\* editing

\* cursor insertion

\* link clicking

\* inline widgets

\* folding

\* toolbar-heavy UI

\* context-sensitive editing controls



This narrow interaction scope is what makes the custom-rendered architecture practical.



\---



\# Selection Model



Implement a lightweight text selection system.



\## Requirements



\* mouse down starts selection

\* drag updates selection

\* mouse up finalizes selection

\* selection paints correctly across:



&#x20; \* lines

&#x20; \* paragraphs

&#x20; \* headings

&#x20; \* code blocks

&#x20; \* quotes

\* `Ctrl+C` copies plain text selection

\* empty click clears selection



\## Internal model



Selection should be stored in logical document text coordinates, not screen coordinates.



Suggested shape:



```cpp

struct TextPosition {

&#x20;   size\_t blockIndex;

&#x20;   size\_t textOffsetInBlock;

};



struct SelectionRange {

&#x20;   TextPosition anchor;

&#x20;   TextPosition focus;

&#x20;   bool isActive;

};

```



You may later normalize to ordered ranges.



\---



\# Scrolling Model



Implement a simple vertical scroll model.



\## Requirements



\* support wheel scrolling

\* support trackpad smooth scrolling where feasible

\* clamp scroll offset

\* relayout on width changes

\* maintain stable selection painting under scrolling

\* redraw only what is necessary



\## Model



\* store `scrollY`

\* document layout computes `documentHeight`

\* viewport height comes from platform window

\* scroll offset affects render transform / visible block range



\---



\# Markdown Feature Support Plan



\## v1 required block features



\* heading levels 1-6

\* paragraph

\* unordered list

\* ordered list

\* blockquote

\* fenced code block

\* thematic break



\## v1 required inline features



\* plain text

\* emphasis

\* strong emphasis

\* inline code

\* links rendered as styled text only



\## Optional for v1.1



\* tables

\* images

\* strikethrough

\* task lists



Avoid overloading v1.

A clean subset with strong rendering quality is better than partial support for everything.



\---



\# Layout Engine Plan



\## Core responsibility



The layout engine must convert the Markdown document model into positioned visual fragments.



\## Suggested concepts



```cpp

struct InlineRun {

&#x20;   TextStyle style;

&#x20;   std::string\_view text;

&#x20;   size\_t sourceStart;

&#x20;   size\_t sourceEnd;

};



struct LineFragment {

&#x20;   float x;

&#x20;   float y;

&#x20;   float width;

&#x20;   float height;

&#x20;   std::vector<InlineRun> runs;

};



struct BlockLayout {

&#x20;   BlockType type;

&#x20;   float x;

&#x20;   float y;

&#x20;   float width;

&#x20;   float height;

&#x20;   std::vector<LineFragment> lines;

};

```



You may need richer structures for selection mapping and hit-testing.



\## Layout rules



\* define a document column width

\* wrap lines to that width

\* place blocks vertically with spacing rules

\* maintain internal text offset mapping for each line/run

\* use consistent padding/margins per block type



\---



\# Rendering Plan



\## Render order



1\. window background

2\. document column background if any

3\. block backgrounds

4\. selection highlights

5\. text

6\. block decorations

7\. optional edge fades / shadow if desired



\## Render responsibilities by block type



\### Paragraph



\* normal text

\* standard line height



\### Headings



\* larger font size

\* stronger weight

\* increased spacing above/below



\### Code blocks



\* monospaced font

\* rounded rectangle background

\* inner padding



\### Blockquotes



\* left accent bar

\* slightly different text color

\* indentation



\### Lists



\* bullet or number markers

\* hanging indentation



\### Horizontal rules



\* subtle line separator with vertical spacing



\---



\# Hit Testing Plan



To support selection, implement hit testing from screen coordinates to logical text positions.



\## Requirements



\* map mouse position to nearest line

\* map x position into nearest text cluster

\* support dragging above/below viewport

\* support selection across blocks



\## Strategy



Each laid-out line should retain enough metadata to map:



\* visual x,y

\* text run ranges

\* grapheme/cluster boundaries if feasible



The hit test should return the nearest valid text position.



\---



\# Clipboard Plan



The copied selection should be \*\*plain text only\*\* in v1.



\## Requirements



\* preserve line breaks sensibly

\* preserve paragraph boundaries

\* preserve code block content text

\* do not copy Markdown syntax unless intentionally selecting source text representation is part of the model



Important design choice:

The app is a \*\*viewer of rendered Markdown\*\*, so selection/copy should copy the \*\*rendered text content\*\*, not raw Markdown markup.



Example:



\* `\*\*Hello\*\*` copies as `Hello`

\* backticks around inline code are not copied unless intentionally represented as visible content



\---



\# Platform Layer Plan



\## Windows v1 platform implementation



Implement a thin Win32 shell:



\* `WinMain`

\* custom window class

\* message loop

\* high-DPI awareness

\* resize handling

\* mouse / keyboard input

\* file open dialog

\* clipboard access

\* drag-and-drop support

\* Skia surface integration



\## Future platform abstraction



Define abstract interfaces for:



\* window events

\* clipboard

\* file open

\* redraw scheduling

\* time / animation hooks if ever needed



Then provide implementations for:



\* Windows

\* macOS later

\* Linux later



Do not over-abstract too early.

The shared interface should remain small.



\---



\# Skia Integration Plan



The coding AI should choose one Skia integration path for Windows and structure it so other platforms can follow later.



\## Suggested initial rendering backend



For Windows v1:



\* use a Skia backend that is practical and stable for desktop rendering

\* keep the surface creation isolated behind a platform rendering adapter



Example abstraction:



```cpp

class RenderSurface {

public:

&#x20;   virtual \~RenderSurface() = default;

&#x20;   virtual int width() const = 0;

&#x20;   virtual int height() const = 0;

&#x20;   virtual SkCanvas\* beginFrame() = 0;

&#x20;   virtual void endFrame() = 0;

};

```



The actual Skia surface creation details should remain hidden in the Windows platform implementation.



\---



\# Milestones



\## Milestone 1: Project skeleton



Deliverables:



\* CMake project

\* Windows app entry point

\* native window

\* redraw loop

\* Skia clears window background



Success criteria:



\* app launches and paints a custom background



\---



\## Milestone 2: Build system and dependency integration



Deliverables:



\* CMake setup

\* `build.ps1`

\* Skia integration

\* Debug and Release builds

\* optional Ninja support

\* MSVC environment import in PowerShell



Success criteria:



\* project builds from PowerShell on Windows

\* Skia links correctly

\* executable launches



\### Important build requirement



As with your previous app, generate a \*\*PowerShell build script\*\* that:



\* imports the Visual Studio environment

\* supports Ninja optionally

\* configures and builds CMake

\* handles generator mismatch

\* supports clean builds



Use the same build philosophy as the previous plan.



\---



\## Milestone 3: File loading



Deliverables:



\* command line file open

\* file open dialog

\* drag-and-drop

\* UTF-8 file loading



Success criteria:



\* app can open Markdown file and store source text



\---



\## Milestone 4: Markdown parsing and document model



Deliverables:



\* parse Markdown into AST or intermediate model

\* convert to internal document representation

\* support required block and inline feature subset



Success criteria:



\* parsed structure is correct for basic Markdown files



\---



\## Milestone 5: Typography and layout engine



Deliverables:



\* paragraph layout

\* heading layout

\* basic list layout

\* code block layout

\* blockquote layout

\* total document height computation



Success criteria:



\* document renders with clean spacing and correct wrapping



\---



\## Milestone 6: Rendering



Deliverables:



\* draw all required block types

\* implement theme colors

\* implement reading column

\* implement clipping and viewport rendering



Success criteria:



\* Markdown document looks polished and readable



\---



\## Milestone 7: Scrolling



Deliverables:



\* wheel scrolling

\* scrollbar or native equivalent

\* viewport clipping

\* stable repaint behavior



Success criteria:



\* long documents scroll smoothly



\---



\## Milestone 8: Selection and copy



Deliverables:



\* hit testing

\* drag selection

\* selection painting

\* clipboard copy via `Ctrl+C`



Success criteria:



\* text can be selected and copied correctly



\---



\## Milestone 9: Polish



Deliverables:



\* app title updates with file name

\* dark/light theme

\* improved spacing and rendering refinement

\* graceful error messages

\* empty file handling



Success criteria:



\* app feels finished and coherent



\---



\# Build System Requirements



\## CMake



Use modern CMake and structure the project cleanly.



Required properties:



\* Windows build support first

\* C++17 or C++20

\* warnings enabled

\* Unicode build

\* Skia integration configurable

\* optional static runtime if desired



\## PowerShell build script



Create `build.ps1` with these capabilities:



\* strict mode

\* MSVC auto-import via `vswhere.exe` + `vcvars64.bat`

\* optional `-UseNinja`

\* `-Clean`

\* `-Configuration Debug|Release`

\* optional custom build directory

\* generator mismatch cleanup

\* optional run smoke test



Recommended parameters:



```powershell

\[CmdletBinding()]

param (

&#x20;   \[switch]$Clean,

&#x20;   \[switch]$UseNinja,

&#x20;   \[ValidateSet("Debug", "Release")]

&#x20;   \[string]$Configuration = "Release",

&#x20;   \[string]$BuildDir = "",

&#x20;   \[string]$Target = "mdviewer",

&#x20;   \[switch]$RunSmokeTest

)

```



Smoke test idea:



\* launch executable with a sample Markdown file

\* verify process starts successfully



\---



\# Performance Requirements



The app should aim for:



\* very fast startup

\* low idle memory use

\* efficient rendering of long documents

\* no browser engine

\* no heavyweight framework runtime

\* layout recalculation only when needed:



&#x20; \* file changes

&#x20; \* window width changes

&#x20; \* theme changes



Optimize for:



1\. simplicity

2\. text rendering quality

3\. responsiveness

4\. maintainability



\---



\# Error Handling Requirements



Show clear native error messages for:



\* file open failure

\* unsupported encoding

\* Markdown parse failure if applicable

\* Skia initialization failure

\* clipboard errors

\* empty document

\* missing font fallback if needed



Use standard native message boxes in v1.



\---



\# Future Cross-Platform Strategy



Design Windows v1 so the shared layers remain reusable.



\## Shared across all platforms



\* Markdown parsing

\* document model

\* layout engine

\* selection model

\* scrolling model

\* rendering commands / paint logic

\* theme system



\## Platform-specific



\* window creation

\* event loop

\* Skia surface binding

\* clipboard

\* file dialogs

\* drag-and-drop

\* DPI / scaling integration



\## Porting order recommendation



1\. Windows

2\. macOS

3\. Linux



Do not attempt all three at once.



\---



\# Non-Goals



The coding AI should explicitly avoid:



\* building a generic widget toolkit

\* implementing editing

\* supporting every Markdown extension immediately

\* embedding Chromium/WebView

\* creating a multi-pane IDE-style UI

\* over-engineering the platform abstraction too early

\* adding plugins or scripting



\---



\# Final instruction to the coding AI



Implement a \*\*small, high-quality, read-only Markdown viewer\*\* using:



\* \*\*C++\*\*

\* \*\*Skia\*\*

\* \*\*a thin native platform layer\*\*

\* \*\*custom-rendered document layout\*\*

\* \*\*only scrolling + text selection + copy as interaction\*\*

\* \*\*Windows first\*\*

\* \*\*cross-platform-ready architecture\*\*



The result should feel like a \*\*beautiful, lightweight document reader\*\*, not a browser shell and not a full text editor.

