# Simple Markdown Viewer

Simple Markdown Viewer is a native, read-only Markdown viewer with Windows and Linux hosts built on shared viewer logic.

Download the latest ready-to-run Windows build from the repository's `Releases` page.

It is built with:

- C++20
- Win32 or GLFW/GTK for the platform shell and event loop
- Skia for custom rendering
- md4c for Markdown parsing
- utf8proc for portable Unicode heading anchors
- Tree-sitter for parser-based code syntax highlighting

## Screenshot

![Simple Markdown Viewer screenshot](assets/screenshot.png)

## Download

If you just want to use the app, go to `Releases` and download the latest `mdviewer-windows-x64.zip`.

The release archive contains:

- `mdviewer.exe`
- `LICENSE`
- `THIRD_PARTY_NOTICES`

Extract the zip to a folder of your choice and run `mdviewer.exe`.

## Features

- Open Markdown and plain text files from:
  - drag and drop
  - `File -> Open...`
  - `File` recent files list on Windows and Linux
  - command-line file argument
  - clicking internal file links
- Save the currently open Markdown document as PDF from `File -> Save as PDF...`
- Print the currently open Markdown document with the native system print dialog on Windows and Linux from `File -> Print...`
- Render:
  - paragraphs
  - headings
  - unordered and ordered lists
  - GitHub-style task lists
  - blockquotes
  - fenced code blocks with **one-click copy**, Tree-sitter syntax highlighting, and per-block horizontal scrolling for long source lines
  - thematic breaks
  - tables with horizontal scrolling when their useful column widths exceed the viewport
  - combinable emphasis, strong text, strikethrough, inline code, and links
  - CommonMark soft breaks as flowing whitespace and explicit hard breaks as visible line breaks
  - a browser-free HTML allowlist for GitHub-style centered paragraphs/headings, links, local images, `<br>`, keyboard keys, subscript/superscript, and native collapsible details; unsupported or unsafe HTML remains visible as source
  - GitHub-style note, tip, important, warning, and caution alerts with native colors and icons
  - decoded Markdown entities
  - **Local raster and SVG images** with aspect-ratio preservation, fit-to-column scaling, and no forced upscaling beyond intrinsic size; SVG HTML `<foreignObject>` content is not supported
- Navigation:
  - **Full browsing history** (back/forward)
  - Toolbar navigation buttons
  - Mouse side-button support on Windows
- Link Handling:
  - Web links open in your default browser
  - Local Markdown and text files open in the same window
  - Robust detection for extensionless files (like `LICENSE`)
  - Clear feedback for missing local files and heading fragments
  - Confirmation before opening executable local files
  - **Link hover preview** at the bottom-left
  - `Ctrl+Click` to force any link to open externally
- Smooth scrolling with:
  - mouse wheel
  - custom scrollbar
  - middle-mouse auto-scroll on Windows
- Responsive document margins that reclaim reading width in narrow windows
- Safe UTF-8 boundary wrapping for very long unbroken strings and URLs
- Remote images are not fetched inside the app; HTML badges and other remote images use labeled placeholders and can still be opened explicitly
- Mouse text selection and `Ctrl+C` copy
- In-document search with `Ctrl+F`, match highlighting, and next/previous navigation
- Search can also be opened from `View -> Find...`
- Native right-click context menu for selection/link actions, reload, document-path copying, and opening files in the file manager
- Image context actions for opening local or remote images, copying image paths, and opening local images in the file manager
- Table context actions for copying tabular data as TSV or correctly quoted CSV
- YAML, TOML, and conservatively recognized JSON front matter rendered as a compact bar for title, author, date, and tags; other raw fields stay hidden
- Markdown file recognition for `.md`, `.markdown`, `.mdown`, and `.mkd`
- Link text remains selectable while links stay clickable
- Switchable `Light`, `Sepia`, and `Dark` themes
- Custom client-drawn menu bar
- Runtime font selection
- Reader zoom controls with toolbar `+` / `-` and `Ctrl` + `+` / `-`
- Manual document reload with `F5`, plus automatic reload on external file changes while the file is open on Windows
- Persistent per-user settings in `mdviewer.ini` for theme, reading font, zoom level, outline side/width, recent files with opened timestamps and scroll positions, and window placement on Windows and Linux
- Platform app icons for the Windows executable and Linux window/desktop integration

## Scope

Current scope:

- native Windows and Linux hosts
- read-only
- single-window
- custom-rendered

Out of scope:

- Markdown editing
- browser or webview rendering
- multi-document workspace UI
- full rich-text editor behavior

## Architecture Status

The codebase is no longer centered around one large Windows source file.

Current structure:

- `src/app/`: shared application state, config, document loading, link resolution, and controller logic
- `src/render/`: shared themes, typography, document rendering, typeface management, and image caching
- `src/render/syntax/`: Tree-sitter code-block highlighting and language/query mapping
- `src/view/`: shared hit testing and document interaction helpers
- `src/platform/win/`: Win32 host code split into bootstrap, window dispatch, menus, dialogs, clipboard, shell, surface, host orchestration, and input translation
- `src/platform/linux/`: Linux host code built on the same shared controller, rendering, and interaction layers

Important Windows files:

- `win_main.cpp`: process startup and bootstrap
- `win_app.cpp`: owns controller/surface/cache wiring for the Windows host
- `win_window.cpp`: main window message dispatch
- `win_viewer_host.cpp`: document load, relayout, render, theme/font/zoom orchestration
- `win_interaction.cpp`: pointer, keyboard, wheel, drag, and timer behavior
- `win_menu.cpp`: Win32 `HMENU` resources, owner-draw popup menus, recent-file menu rebuilding, and command IDs

Linux host files:

- `linux_main.cpp`: GLFW startup and event loop
- `linux_app.cpp`: app-scoped controller/config wiring
- `linux_viewer_host.cpp`: document load, relayout, render, theme/font/zoom orchestration
- `linux_interaction.cpp`: GLFW input translation into shared interaction/controller actions
- `linux_menu.cpp`: Linux dropdown command models
- `linux_context_menu.cpp`, `linux_file_dialog.cpp`, `linux_font_dialog.cpp`: GTK-backed native helpers
- `linux_clipboard.cpp`, `linux_shell.cpp`, `linux_surface.cpp`: platform services

Recent refactor work moved top-bar layout, drawing, toolbar hit testing, and dropdown drawing into shared rendering code in `src/render/menu_renderer.*`. Platform hosts still own native popup/dropdown command plumbing and event translation. The menu UI typeface is independent from the document font. Windows also has native file watching for live reload.

Code block syntax highlighting is implemented in the shared layout/rendering path. Fenced code block languages currently supported by Tree-sitter are `c`, `cpp`, `javascript`, `typescript`, `tsx`, `json`, `python`, `bash`/`sh`, `rust`, `go`, and `csharp`; unknown languages fall back to plain code rendering.

Windows has release packaging today. Linux is implemented in-tree and builds from the same CMake project on Linux.

## Windows Build Requirements

- Windows
- Visual Studio 2022 with C++ build tools
- Python
- Git
- Network access for the first dependency fetch, unless `build/_deps` is already populated

The PowerShell build script imports the Visual Studio environment automatically with `vswhere` and `vcvars64.bat`.

## Building On Windows

First build, including dependency setup:

```powershell
.\build.ps1 -Configuration Release
```

Subsequent local builds when Skia is already available:

```powershell
.\build.ps1 -SkipSkia -Configuration Release
```

Useful variants:

```powershell
.\build.ps1 -Clean -SkipSkia -Configuration Release
.\build.ps1 -Configuration Debug
.\build.ps1 -RunSmokeTest
```

## GitHub Builds And Releases

- Normal branch pushes and pull requests do not run GitHub builds; development builds and tests are run locally.
- GitHub Actions builds Windows and Linux only for pushed release tags matching `v*`, or when a workflow is started manually.
- The Windows release workflow prefers a prebuilt Skia bundle so it does not normally rebuild Skia from source. Skia source builds use the exact commit in `ci/skia-revision.txt`; Windows bundle maintenance is documented in [`docs/WINDOWS_SKIA_BUNDLE.md`](docs/WINDOWS_SKIA_BUNDLE.md).
- The prebuilt Skia bundle must be built with `skia_enable_pdf=true` for PDF export support.
- Release workflow runs upload `mdviewer-windows-x64.zip` and `mdviewer-linux-x64.tar.gz` as build artifacts.
- Pushing a tag like `v0.1.5` creates or updates a GitHub release and attaches both platform archives plus the Linux SHA-256 checksum after their respective builds succeed.
- Release archives contain the executable, `LICENSE`, `THIRD_PARTY_NOTICES`, and supporting platform metadata where applicable.

Default output:

```text
build/Release/mdviewer.exe
```

## Running

Launch the viewer:

```powershell
.\build\Release\mdviewer.exe
```

Open a file immediately:

```powershell
.\build\Release\mdviewer.exe .\README.md
```

The app stores `mdviewer.ini` in the per-user config directory and uses it for theme, font, zoom, outline side/width, recent-file timestamps and scroll positions, and the last window size and position on Windows and Linux:

- Windows: `%APPDATA%\Simple Markdown Viewer\mdviewer.ini`
- Linux: `$XDG_CONFIG_HOME/simple-markdown-viewer/mdviewer.ini`, or `~/.config/simple-markdown-viewer/mdviewer.ini` when `XDG_CONFIG_HOME` is not set

For compatibility, if the per-user file does not exist, the app can still load a legacy `mdviewer.ini` next to the executable. Future saves go to the per-user path.

## Linux Build Notes

The Linux host is compiled from the same CMake target on Linux. It uses GLFW for the window/event loop and GTK3 for native dialogs/context menus, alongside the same Skia, md4c, utf8proc, and Tree-sitter dependencies.

Build Skia and the PDF-enabled viewer, then run the tests:

```bash
./build.sh
cmake --build build --target mdviewer_tests --parallel 2
ctest --test-dir build --output-on-failure
```

For subsequent builds, use `./build.sh --skip-skia`. PDF-enabled Linux Skia builds disable HarfBuzz PDF font subsetting because the pinned subsetter can crash on common system fonts; `--skip-skia` rejects older incompatible Skia output. A build against Skia without its PDF backend can be configured with `./build.sh --disable-pdf`; that build omits the Linux PDF menu command and reports the backend as unavailable through the shared export API.

The Linux release workflow performs a normal build/test pass and a second unit-test pass with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

Create a stripped Linux release archive with:

```bash
./package-linux.sh
```

If the Release application has already been built, use `./package-linux.sh --skip-build`. The resulting archive and checksum are written to `dist/mdviewer-linux-x64.tar.gz` and `dist/mdviewer-linux-x64.tar.gz.sha256`. Skia, md4c, utf8proc, GLFW, and Tree-sitter are linked into the executable; GTK3, OpenGL, X11, fontconfig, freetype, and standard C/C++ runtime libraries remain distribution-provided dependencies. See [`docs/LINUX_SMOKE_TEST.md`](docs/LINUX_SMOKE_TEST.md) for release validation and manual desktop checks.

## Controls

- `File -> Open...`: open a file
- `File -> Reload` or `F5`: reload the current document and preserve its reading position when practical
- `File -> Save as PDF...`: export the currently open Markdown document to PDF
- `File -> Print...` or `Ctrl+P`: print the currently open Markdown document on Windows or Linux
- `File`: reopen recently opened files on Windows or Linux; the newest file appears first with its last-opened date and time
- drag and drop: open a file
- mouse wheel: scroll
- `Ctrl` + mouse wheel: zoom document text in and out
- Code blocks: drag the horizontal scrollbar, use a horizontal touchpad gesture, or hold `Shift` while using the mouse wheel
- middle mouse button: auto-scroll mode on Windows
- left mouse drag: select text
- `Ctrl+C`: copy selected text
- `Ctrl+F`: search within the current document
- `F5`: reload the current document while preserving the reading position when practical
- `Enter` / `Shift+Enter`: move to the next or previous search match while search is open
- `Escape`: close search
- Search close button: click the `x` button in the search box
- right click: open a native context menu with selection, link, reload, document-path, and file-manager actions
- external file save: reload the currently open document automatically on Windows
- `View -> Select Font...`: choose the reading font
- `View -> Theme`: switch between light, sepia, and dark themes
- `View -> Show Outline`: show or collapse the heading outline with `Ctrl+Shift+O`
- `View -> Outline on Left/Right`: choose the outline side; saved as `outline_side=left` or `outline_side=right`
- Outline sidebar: use the mouse wheel or scrollbar for long heading lists; drag the divider to resize it. The width is saved as `outline_width`.
- `Ctrl` + `+` / `-`: zoom document text in and out
- **Navigation**:
  - `Alt + Left` or `Backspace`: Go Back
  - `Alt + Right`: Go Forward
  - `Left / Right Arrow`: Go Back/Forward (if no text is selected)
  - Mouse side buttons: Go Back/Forward on Windows
  - Toolbar buttons: Click the arrows in the top-right corner
- **Zoom**:
  - Toolbar buttons: Click `+` or `-` in the top-right corner
- **Links**:
  - `Click`: Open internally (MD/Text) or externally (Web/Other)
  - `Ctrl + Click`: Force open in default system application
  - `Right Click`: open/copy links and open local targets in the file manager from the native context menu
  - Executable local files require confirmation before opening
  - `Hover`: Preview target path in bottom-left overlay
  - `Click and drag`: select link text without opening the link
- **Code Blocks**:
  - Click the **icon in the top-right corner** of a code block to copy its entire content

## Dependencies

Direct dependencies used by the current build:

- `Skia`
  - role: 2D rendering, text drawing, and PDF generation
  - license: BSD-3-Clause-style
  - local license file: `third_party/skia/LICENSE`
- `md4c`
  - role: Markdown parsing
  - version: `release-0.5.2`
  - license: MIT
  - local license file: `build/_deps/md4c-src/LICENSE.md`
- `utf8proc`
  - role: portable Unicode case conversion for heading anchors
  - version: `v2.11.3`
  - license: MIT, including Unicode data under the bundled Unicode data license
  - local license file: `build/_deps/utf8proc-src/LICENSE.md`
- `Tree-sitter`
  - role: parser-based syntax highlighting for fenced code blocks
  - license: MIT
  - local license file: `build/_deps/tree_sitter-src/LICENSE`
- Tree-sitter grammars for `c`, `cpp`, `javascript`, `typescript`/`tsx`, `json`, `python`, `bash`, `rust`, `go`, and `c-sharp`
  - role: language parsers and highlight queries
  - license: MIT
  - local license files: `build/_deps/tree_sitter_*-src/LICENSE`
- `GLFW`
  - role: Linux native window/event integration
  - version: `3.3.8`
  - license: Zlib
- `GTK3`
  - role: Linux native file, font, and context menu helpers
  - license: LGPL-family GTK license

Windows system libraries linked by the app:

- `windowscodecs`
- `dwrite`
- `usp10`
- `ole32`
- `user32`
- `gdi32`
- `shell32`

Linux system libraries linked by the app include:

- `fontconfig`
- `freetype`
- `pthread`
- `dl`
- `GL`
- `X11`

## Licensing

This project is licensed under the MIT License. See [LICENSE](LICENSE).

Third-party dependency notices are included in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES).

## Repository Layout

```text
src/
  app/            Shared app state, config, controller, loading, links
  layout/         Document layout and text flow
  markdown/       Markdown parsing into the internal model
  render/         Shared themes, typography, renderer, typefaces, image cache
  render/syntax/  Tree-sitter code-block syntax highlighting
  view/           Shared hit testing and interaction logic
  platform/linux/ Linux host integration
  platform/win/   Win32 bootstrap, window dispatch, menus, input, and host code
  util/           File I/O and font helpers

resources/
  app.rc
  app_icon.ico

assets/
  screenshot.png

build.ps1         Windows build script
CMakeLists.txt    CMake project definition
```

## Notes

- The viewer copies rendered text content, not raw Markdown markup.
- Search matches rendered document text, not raw Markdown source.
- Syntax highlighting uses Tree-sitter for fenced code blocks with known language tags; unknown languages render as plain code.
- Markdown source is passed to md4c unchanged; fenced and indented code is never rewritten by a normalization pass.
- The app has native Windows and Linux hosts sharing the same document/controller/render/view layers.
- The menu bar is client-drawn so it can follow the selected theme; shared layout/drawing helpers live in `src/render/menu_renderer.*`.
- The document zoom affects rendered document typography, not the top menu bar.
- PDF export uses Skia's PDF backend and the shared document renderer with PDF-specific page margins and page-break handling. Export typography is tuned to visually approximate the interactive viewer at 100%. Wide tables and code blocks are reduced locally within a readability limit, then clipped if they still exceed the page; interactive scrollbars are never rendered into output.
- Windows and Linux printing use native system print dialogs and the same shared paginated renderer as PDF export. The host print paths do not provide an in-app page preview; that remains tracked separately.
- On Windows, live reload is event-driven via OS file-change notifications rather than polling.
- Recent refactor work moved config, controller, rendering support, interaction logic, and most host orchestration out of the old monolithic Windows entry file.
