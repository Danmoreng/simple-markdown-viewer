# MD Viewer Plan

## Goal

Build a fast, lightweight, read-only Markdown viewer with:

- native platform windows and event loops
- Skia-based custom rendering
- shared document/layout/rendering logic
- thin platform hosts for Windows first, Linux next

This is a document viewer, not a generic UI toolkit and not an editor.

## Current Status

The project has a working Windows implementation with:

- Markdown parsing via `md4c`
- custom Skia document rendering
- scrolling
- text selection and copy
- link rendering and activation
- image rendering
- navigation history
- theme switching
- code block copy buttons
- Tree-sitter syntax highlighting for fenced code blocks

The large early architectural bottleneck in `win_main.cpp` has now been substantially reduced.

Completed refactor work so far:

- persistent config in `app_config` for theme, font family, base font size, and recent files
- shared theme and typography modules in `src/render/`
- shared controller logic in `src/app/viewer_controller.*`
- shared hit testing and interaction helpers in `src/view/`
- shared document typeface and image cache helpers in `src/render/`
- shared Tree-sitter code block highlighting in `src/render/syntax/`
- extracted Windows helpers for clipboard, file dialog, shell, surface, interaction, host orchestration, window dispatch, and app bootstrap

The Windows host is now split across:

- [src/platform/win/win_main.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_main.cpp)
- [src/platform/win/win_app.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_app.cpp)
- [src/platform/win/win_window.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_window.cpp)
- [src/platform/win/win_viewer_host.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_viewer_host.cpp)
- [src/platform/win/win_interaction.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_interaction.cpp)
- [src/platform/win/win_menu.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_menu.cpp)

The main remaining Windows-specific architectural question before Linux is no longer "split `win_main.cpp` at all costs". It is:

- finish the cleanup around the custom top bar / menu layer and confirm the final platform-vs-shared boundary for that UI

Remaining platform-heavy areas are mostly:

- [src/platform/win/win_menu.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_menu.cpp) for custom menu bar state, drawing, and command hit testing
- [src/platform/win/win_window.cpp](/C:/Development/simple-markdown-viewer/src/platform/win/win_window.cpp) for message dispatch and event translation
- the lack of targeted automated tests around config/history/link-resolution/layout behavior

That remaining cleanup should happen before the Linux implementation starts.

## Immediate Objectives

1. Finish the remaining Windows cleanup around custom menu/top-bar ownership and message translation.
2. Add small automated tests around config parsing, history/navigation, link resolution, and relayout-sensitive behavior.
3. Confirm which UI pieces stay platform-specific versus shared before introducing a second host.
4. Start the Linux host only after the Windows boundary is stable.

## Product Rules

Keep these constraints:

- single window
- read-only
- custom-rendered document surface
- no browser embedding
- no editing
- no general widget framework
- limited interaction surface

Supported interactions for v1:

- open file
- drag and drop file
- scroll
- select text
- copy selection
- activate links
- navigate history
- zoom in/out
- switch theme

## Refactor Direction

### 1. Thin Platform Host

Platform-specific code should only handle:

- app entry point
- window creation and destruction
- native event loop
- clipboard API
- file open dialog
- drag and drop integration
- external shell open
- timers
- invalidation
- cursor/capture
- Skia surface binding
- DPI and resize integration

The platform host must not own Markdown parsing, document rendering decisions, theme logic, or navigation policy.

### 2. Shared Application Layer

Create a shared controller that owns:

- current file path
- current source text
- parsed document model
- current layout
- history stack and index
- current theme
- selected font family
- base font size
- zoom limits
- link/navigation policy
- relayout requests

This layer should expose platform-agnostic actions such as:

- `OpenFile(path)`
- `ReloadCurrentFile()`
- `GoBack()`
- `GoForward()`
- `SetTheme(theme)`
- `SetFontFamily(name)`
- `ResetFontFamily()`
- `SetBaseFontSize(size)`
- `ZoomIn()`
- `ZoomOut()`
- `ResetZoom()`

### 3. Shared Rendering Layer

Move Skia drawing logic into shared rendering files.

This layer should handle:

- document background
- text drawing
- selection highlights
- block decorations
- code block chrome
- syntax-highlighted code tokens for supported fenced languages
- scrollbar visuals if kept custom
- hover/copy overlays if they remain part of the custom document UI
- theme palettes
- typography derived from the base font size

### 4. Shared Interaction Layer

Move document interaction behavior into shared files:

- hit testing
- click-vs-drag selection behavior
- link click disambiguation
- selection updates
- scroll helpers
- code block button hit regions

This should operate on generic coordinates and shared app/view state, not on Win32 messages directly.

## Planned Source Layout

Target structure:

```text
/src/app/
  app_state.h
  viewer_controller.h
  viewer_controller.cpp
  app_config.h
  app_config.cpp

/src/render/
  theme.h
  theme.cpp
  typography.h
  typography.cpp
  document_renderer.h
  document_renderer.cpp
  syntax/
    tree_sitter_highlighter.h
    tree_sitter_highlighter.cpp

/src/view/
  document_interaction.h
  document_interaction.cpp
  hit_test.h
  hit_test.cpp

/src/platform/
  platform_services.h

/src/platform/win/
  win_main.cpp
  win_window.h
  win_window.cpp
  win_menu.cpp
  win_clipboard.cpp
  win_file_dialog.cpp
  win_shell.cpp
  win_surface.cpp

/src/platform/linux/
  linux_main.cpp
  linux_window.cpp
  linux_clipboard.cpp
  linux_file_dialog.cpp
  linux_shell.cpp
  linux_surface.cpp
```

The exact filenames may differ, but the layering should remain.

## Configuration Plan

Add an app config file with persistent settings.

Required persisted settings:

- selected theme
- selected font family
- base font size

Recommended file name:

- `mdviewer.ini`

Lookup strategy:

1. prefer config next to the executable for portable/dev builds
2. if that location is not writable, fall back to a per-user config location later

The config format should be simple INI-style text. Example:

```ini
[app]
theme=dark
font_family=Georgia
base_font_size=16.0

[theme.light]
window_background=#F2EFE7
body_text=#493A29
```

Rules:

- built-in themes remain defined in code
- config values act as overrides
- invalid or missing config entries fall back to safe defaults
- saving config should be explicit and robust

## Theme Plan

Move theme definitions out of the Windows file and into shared render/theme files.

Themes should include:

- built-in palette definitions
- optional config overrides
- current active theme selection

Theme switching must:

- update app state
- persist to config
- trigger relayout/repaint only when necessary

## Typography and Zoom Plan

The app needs one shared base font size that drives all rendered typography.

The base font size should scale:

- paragraph text
- headings
- blockquotes
- inline code
- fenced code blocks
- menu/top-bar document controls if those remain custom-rendered
- overlay text where appropriate

Implementation rules:

- keep the current typography ratios, but derive them from one base size
- use clamped zoom bounds, for example `10.0f` to `32.0f`
- relayout the document whenever the base font size changes
- persist the base font size in config

User controls:

- top-bar `+` button for zoom in
- top-bar `-` button for zoom out
- `Ctrl` + `+` for zoom in
- `Ctrl` + `-` for zoom out
- `Ctrl` + mouse wheel zoom is optional, not required yet

Behavior:

- zoom changes the rendered document scale by changing the typography inputs, not by applying a canvas transform
- zoom affects layout metrics and line wrapping
- zoom should preserve the reading position as well as practical

## Linux Preparation

The Linux implementation should reuse:

- Markdown parsing
- document model
- layout engine
- Tree-sitter syntax highlighting
- controller/state
- rendering logic
- interaction logic
- config/theme handling

Linux-specific code should be limited to:

- native window/event integration
- clipboard
- file dialog
- drag and drop
- external open
- Skia surface creation

Do not start Linux until the Windows-specific file has been split and the shared controller/render/interaction layers exist.

## Refactor Phases

### Phase 1: Config and Theme Extraction

- status: complete
- add `app_config`
- add persistent `theme`, `font_family`, and `base_font_size`
- move theme palettes out of `win_main.cpp`

### Phase 2: Typography and Zoom

- status: complete
- introduce shared typography scaling from `base_font_size`
- wire `+` and `-` UI controls
- wire `Ctrl` + `+` and `Ctrl` + `-`
- persist zoom/base font size

### Phase 3: Controller Extraction

- status: complete
- move file loading
- move history management
- move relayout triggers
- move theme/font/zoom actions
- reduce Win32 code to dispatching actions

### Phase 4: Renderer and Interaction Extraction

- status: complete
- move drawing logic into shared render files
- move hit testing and selection logic into shared view files
- leave only platform event translation in Win32

### Phase 5: Windows Cleanup

- status: in progress
- split `win_main.cpp` into entry, window, menu, shell, clipboard, and surface files
- remove remaining shared logic from platform files
- current state:
  - `win_main.cpp` is now a small bootstrap/entry file
  - `win_window.cpp` owns message dispatch
  - `win_app.cpp` owns Windows app-scoped state wiring
  - `win_viewer_host.cpp` owns document/view orchestration
  - `win_interaction.cpp` owns Win32 interaction translation
  - `win_menu.cpp` still owns the custom top-bar/menu implementation and is the main remaining cleanup target

### Phase 6: Linux Host

- status: not started
- add Linux platform implementation on top of the shared controller/render/view layers

## Non-Goals

Do not spend time on:

- building a general widget toolkit
- implementing editing
- supporting all Markdown extensions immediately
- adding plugin systems
- adding a complex settings UI before config persistence exists
- abstracting every OS detail behind premature interfaces

## Implementation Standard

Prefer:

- explicit ownership
- narrow interfaces
- small files with clear responsibilities
- behavior-preserving refactors before feature expansion
- shared Skia rendering code where feasible
- platform-specific code only where actually required by the OS
