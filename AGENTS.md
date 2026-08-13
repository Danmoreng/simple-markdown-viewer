# Simple Markdown Viewer Agent Guide

This repository contains a native, read-only Markdown viewer. It is a document
viewer, not an editor, browser shell, or general UI toolkit.

Product and engineering milestones belong in `DEVELOPMENT_PLAN.md`. Keep this
file focused on repository orientation, setup, build commands, and architectural
boundaries.

## Repository Overview

The app is built with:

- C++20
- Skia for custom rendering
- `md4c` for Markdown parsing
- Tree-sitter for fenced-code syntax highlighting
- Win32 for the Windows host
- GLFW plus GTK helpers for the Linux host

The Windows and Linux hosts share the document model, Markdown parsing, layout,
rendering, controller/state, config, theme, typography, image cache, syntax
highlighting, hit testing, interaction helpers, and top-bar rendering helpers.

Platform code should stay thin. It should translate native events and provide
native services such as windowing, clipboard, file dialogs, drag and drop,
external shell open, timers, invalidation, cursor/capture, DPI/resize handling,
and Skia surface binding.

## Source Layout

```text
src/
  app/             Shared app state, config, loading, links, controller
  layout/          Document model and layout engine
  markdown/        Markdown parsing
  text/            Shared document font context and text-style resolution
  render/          Shared Skia rendering, themes, typography, image/typeface helpers
  render/syntax/   Tree-sitter syntax highlighting
  util/            File I/O, UTF-8, and font helpers
  view/            Shared hit testing, context menu model, interaction helpers
  platform/win/    Win32 bootstrap, window dispatch, menus, input, services, surface
  platform/linux/  GLFW/GTK Linux host, menus, input, services, surface

resources/         Windows resources
assets/            README assets
test-docs/         Manual/fixture Markdown documents
```

Important shared files:

- `src/app/viewer_controller.*`: shared document loading, config, history, theme/font/zoom actions, link resolution entry points
- `src/render/document_renderer.*`: shared Skia document rendering
- `src/render/menu_renderer.*`: shared client-drawn top-bar layout, drawing, toolbar hit testing, and dropdown drawing
- `src/view/document_interaction.*`: shared selection, search, scroll, keyboard, and click behavior helpers
- `src/view/document_hit_test.*`: shared document hit testing
- `src/app/link_resolver.*`: shared internal/external link policy
- `src/app/app_config.*`: INI-style config parsing and saving

Important Windows files:

- `src/platform/win/win_main.cpp`: Windows entry/bootstrap
- `src/platform/win/win_app.*`: Windows app-scoped controller/surface/cache wiring
- `src/platform/win/win_window.*`: Win32 message dispatch
- `src/platform/win/win_viewer_host.*`: Windows document load, relayout, render, theme/font/zoom orchestration
- `src/platform/win/win_interaction.*`: Win32 input translation
- `src/platform/win/win_menu.*`: Win32 `HMENU`, owner-draw popup menus, recent-file menu rebuilding, and command IDs

Important Linux files:

- `src/platform/linux/linux_main.cpp`: GLFW startup and event loop
- `src/platform/linux/linux_app.*`: Linux app-scoped controller/config wiring
- `src/platform/linux/linux_viewer_host.*`: Linux document load, relayout, render, theme/font/zoom orchestration
- `src/platform/linux/linux_interaction.*`: GLFW input translation
- `src/platform/linux/linux_menu.*`: Linux dropdown command model
- `src/platform/linux/linux_context_menu.*`, `linux_file_dialog.*`, `linux_font_dialog.*`, `linux_print.*`: GTK-backed native helpers
- `src/platform/linux/linux_clipboard.*`, `linux_shell.*`, `linux_surface.*`: Linux platform services

## Build On Windows

Requirements:

- Windows
- Visual Studio 2022 with C++ build tools
- Python
- Git
- Network access for the first dependency fetch unless `build/_deps` and Skia are already available

Primary build script:

```powershell
.\build.ps1 -Configuration Release
```

The Windows release workflow normally consumes a prebuilt Skia bundle because
hosted Windows source builds are expensive. Bundle history, local creation,
provenance, testing, and activation are documented in
`docs/WINDOWS_SKIA_BUNDLE.md`.

Useful variants:

```powershell
.\build.ps1 -SkipSkia -Configuration Release
.\build.ps1 -Clean -SkipSkia -Configuration Release
.\build.ps1 -Configuration Debug
.\build.ps1 -RunSmokeTest
```

Default Windows output:

```text
build/Release/mdviewer.exe
```

Run the built app:

```powershell
.\build\Release\mdviewer.exe
.\build\Release\mdviewer.exe .\README.md
```

## Build On Linux

The Linux host is built from the same CMake project on Linux. It uses GLFW for
windowing/event integration and GTK3 for native dialogs/context menus.

Linux dependencies include Skia, md4c, Tree-sitter, GLFW, GTK3, fontconfig,
freetype, pthread, dl, OpenGL, and X11. Build the PDF-enabled application and
Skia dependency, then run the tests, with:

```bash
./build.sh
cmake --build build --target mdviewer_tests --parallel 2
ctest --test-dir build --output-on-failure
```

Use `./build.sh --skip-skia` after Skia has already been built. Use
`--disable-pdf` only with a Skia build configured without its PDF backend.
Create the stripped Linux archive with `./package-linux.sh`; release validation
and manual host checks are documented in `docs/LINUX_SMOKE_TEST.md`.

## Configuration

The app stores settings in a per-user `mdviewer.ini`:

- Windows: `%APPDATA%\Simple Markdown Viewer\mdviewer.ini`
- Linux: `$XDG_CONFIG_HOME/simple-markdown-viewer/mdviewer.ini`, or
  `~/.config/simple-markdown-viewer/mdviewer.ini` when `XDG_CONFIG_HOME` is not set

If the per-user file does not exist, the app may load a legacy `mdviewer.ini`
next to the executable for compatibility. Future saves go to the per-user path.
Persisted settings include:

- selected theme
- selected document font family
- base font size / reader zoom
- outline side and width
- Linux window size and position
- recent files with their last-opened timestamps and remembered scroll positions

Theme palette overrides are not supported config keys. Config should persist the
selected named theme only.

Config parsing should be tolerant. Invalid or missing values must fall back to
safe defaults.

## Architectural Rules

- Keep the app single-window and read-only.
- Do not add browser, webview, editor, or general widget-framework behavior.
- Prefer shared code for Markdown parsing, document model, layout, rendering,
  selection, search, link policy, history, theme, typography, config, image
  handling, and syntax highlighting.
- Keep platform code limited to native event translation and native services.
- Keep behavior-preserving refactors separate from product feature work where
  practical.
- Do not start feature work from stale assumptions; check the current code first.

## Current Test State

Focused shared tests live in `tests/unit_tests.cpp` and are registered with
CTest as `mdviewer_unit_tests`. The Linux release workflow builds the application,
runs the normal tests, and repeats them with AddressSanitizer and
UndefinedBehaviorSanitizer. Normal development builds and tests are run locally.
Manual rendering coverage remains in `test-docs/`.
