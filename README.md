# Simple Markdown Viewer

Simple Markdown Viewer is a native, read-only Markdown viewer for Windows.

It is built with:

- C++20
- Win32 for the application shell and event loop
- Skia for custom rendering
- md4c for Markdown parsing

## Screenshot

![Simple Markdown Viewer screenshot](assets/screenshot.png)

## Features

- Open Markdown and plain text files from:
  - drag and drop
  - `File -> Open...`
  - command-line file argument
  - clicking internal file links
- Render:
  - paragraphs
  - headings
  - unordered and ordered lists
  - blockquotes
  - fenced code blocks with **one-click copy**
  - thematic breaks
  - emphasis, strong text, inline code, and links
  - **Images** with aspect-ratio preservation and auto-scaling
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
- Switchable `Light`, `Sepia`, and `Dark` themes
- Custom client-drawn menu bar
- Runtime font selection
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

## Build Requirements

- Windows
- Visual Studio 2022 with C++ build tools
- Python
- Git

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

## Controls

- `File -> Open...`: open a file
- drag and drop: open a file
- mouse wheel: scroll
- middle mouse button: auto-scroll mode
- left mouse drag: select text
- `Ctrl+C`: copy selected text
- `View -> Select Font...`: choose the reading font
- `View -> Theme`: switch between light, sepia, and dark themes
- **Navigation**:
  - `Alt + Left` or `Backspace`: Go Back
  - `Alt + Right`: Go Forward
  - `Left / Right Arrow`: Go Back/Forward (if no text is selected)
  - Mouse side buttons: Go Back/Forward
  - Toolbar buttons: Click the arrows in the top-right corner
- **Links**:
  - `Click`: Open internally (MD/Text) or externally (Web/Other)
  - `Ctrl + Click`: Force open in default system application
  - `Hover`: Preview target path in bottom-left overlay
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
  app/            App state
  layout/         Document layout and text flow
  markdown/       Markdown parsing into the internal model
  platform/win/   Win32 window, input, menus, and rendering host
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
- The app currently targets Windows first, but the architecture is intended to stay portable.
- The menu bar is client-drawn so it can follow the selected theme.
