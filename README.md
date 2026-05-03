# Simple Markdown Viewer

Simple Markdown Viewer is a native, read-only Markdown viewer with a validated Windows build and a Linux host under active integration.

Download the latest ready-to-run Windows build from the repository's `Releases` page.

It is built with:

- C++20
- Win32 for the application shell and event loop
- Skia for custom rendering
- md4c for Markdown parsing
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
  - `File` recent files list
  - command-line file argument
  - clicking internal file links
- Render:
  - paragraphs
  - headings
  - unordered and ordered lists
  - GitHub-style task lists
  - blockquotes
  - fenced code blocks with **one-click copy** and Tree-sitter syntax highlighting
  - thematic breaks
  - tables
  - emphasis, strong text, strikethrough, inline code, and links
  - decoded Markdown entities
  - **Images** with aspect-ratio preservation, fit-to-column scaling, and no forced upscaling beyond intrinsic size
- Navigation:
  - **Full browsing history** (back/forward)
  - Toolbar navigation buttons
  - Mouse side-button support
- Link Handling:
  - Web links open in your default browser
  - Local Markdown and text files open in the same window
  - Robust detection for extensionless files (like `LICENSE`)
  - **Link hover preview** at the bottom-left
  - `Ctrl+Click` to force any link to open externally
- Smooth scrolling with:
  - mouse wheel
  - custom scrollbar
  - middle-mouse auto-scroll
- Mouse text selection and `Ctrl+C` copy
- Link text remains selectable while links stay clickable
- Switchable `Light`, `Sepia`, and `Dark` themes
- Custom client-drawn menu bar
- Runtime font selection
- Reader zoom controls with toolbar `+` / `-` and `Ctrl` + `+` / `-`
- Automatic document reload on external file changes while the file is open
- Persistent settings in `mdviewer.ini` for theme, reading font, zoom level, and recent files
- Embedded Windows app icon

## Scope

Current scope:

- Windows-first
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
- `win_menu.cpp`: custom top bar and menu handling

Recent refactor work also moved top-bar layout and hit testing into shared rendering code, kept the menu UI typeface independent from the document font, and added Windows-native file watching for live reload.

Code block syntax highlighting is implemented in the shared layout/rendering path. Fenced code block languages currently supported by Tree-sitter are `c`, `cpp`, `javascript`, `typescript`, `tsx`, `json`, `python`, and `bash`/`sh`; unknown languages fall back to plain code rendering.

Windows remains the primary validated build target. The Linux host is in-tree, but final runtime validation is still pending.

## Build Requirements

- Windows
- Visual Studio 2022 with C++ build tools
- Python
- Git
- Network access for the first dependency fetch, unless `build/_deps` is already populated

The PowerShell build script imports the Visual Studio environment automatically with `vswhere` and `vcvars64.bat`.

## Building

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

- GitHub Actions builds the Windows release on pushes to `main` and on pull requests.
- CI prefers a prebuilt Windows Skia bundle so normal app builds do not rebuild Skia from source.
- Each workflow run uploads `mdviewer-windows-x64.zip` as a build artifact.
- Pushing a tag like `v0.1.2` also creates or updates a GitHub release and attaches the packaged Windows build.
- Release archives contain `mdviewer.exe`, `LICENSE`, and `THIRD_PARTY_NOTICES`.

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

The app stores `mdviewer.ini` next to the executable and uses it for theme, font, zoom, and recent-file persistence.

## Controls

- `File -> Open...`: open a file
- `File`: reopen recently opened files
- drag and drop: open a file
- mouse wheel: scroll
- middle mouse button: auto-scroll mode
- left mouse drag: select text
- `Ctrl+C`: copy selected text
- external file save: reload the currently open document automatically
- `View -> Select Font...`: choose the reading font
- `View -> Theme`: switch between light, sepia, and dark themes
- `Ctrl` + `+` / `-`: zoom document text in and out
- **Navigation**:
  - `Alt + Left` or `Backspace`: Go Back
  - `Alt + Right`: Go Forward
  - `Left / Right Arrow`: Go Back/Forward (if no text is selected)
  - Mouse side buttons: Go Back/Forward
  - Toolbar buttons: Click the arrows in the top-right corner
- **Zoom**:
  - Toolbar buttons: Click `+` or `-` in the top-right corner
- **Links**:
  - `Click`: Open internally (MD/Text) or externally (Web/Other)
  - `Ctrl + Click`: Force open in default system application
  - `Hover`: Preview target path in bottom-left overlay
  - `Click and drag`: select link text without opening the link
- **Code Blocks**:
  - Click the **icon in the top-right corner** of a code block to copy its entire content

## Dependencies

Direct dependencies used by the current build:

- `Skia`
  - role: 2D rendering and text drawing
  - license: BSD-3-Clause-style
  - local license file: `third_party/skia/LICENSE`
- `md4c`
  - role: Markdown parsing
  - version: `release-0.5.2`
  - license: MIT
  - local license file: `build/_deps/md4c-src/LICENSE.md`
- `Tree-sitter`
  - role: parser-based syntax highlighting for fenced code blocks
  - license: MIT
  - local license file: `build/_deps/tree_sitter-src/LICENSE`
- Tree-sitter grammars for `c`, `cpp`, `javascript`, `typescript`/`tsx`, `json`, `python`, and `bash`
  - role: language parsers and highlight queries
  - license: MIT
  - local license files: `build/_deps/tree_sitter_*-src/LICENSE`

Windows system libraries linked by the app:

- `windowscodecs`
- `dwrite`
- `usp10`
- `ole32`
- `user32`
- `gdi32`
- `shell32`

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
- Syntax highlighting uses Tree-sitter for fenced code blocks with known language tags; unknown languages render as plain code.
- The app currently targets Windows first, with Linux host integration in progress.
- The menu bar is client-drawn so it can follow the selected theme.
- The document zoom affects rendered document typography, not the top menu bar.
- On Windows, live reload is event-driven via OS file-change notifications rather than polling.
- Recent refactor work moved config, controller, rendering support, interaction logic, and most host orchestration out of the old monolithic Windows entry file.
