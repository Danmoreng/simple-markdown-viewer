# Native Mermaid diagrams

The viewer renders fenced `mermaid` blocks using
`mermaid-rs-renderer` **0.3.1**, with `default-features = false`. It uses neither
a browser nor a subprocess. There is no CLI, Node.js, PNG renderer, `resvg`, or
`usvg` dependency. Skia draws the resulting SVG as vectors, including in print
and PDF output. Rust is needed only at build time.

## Build and try

Install Rust/Cargo with a native toolchain matching the C++ target. Windows
x64 uses `x86_64-pc-windows-msvc`; Linux uses its native GNU target. The CI
toolchain is pinned to **1.98.1**, which is also the local validation version.
For an existing rustup installation:

```text
rustup toolchain install 1.98.1 --profile minimal
rustup override set 1.98.1
```

Windows, with the existing Skia libraries:

```powershell
.\build.ps1 -SkipSkia -Configuration Release
cmake --build build --config Release --target mdviewer_tests
ctest --test-dir build -C Release --output-on-failure
.\build\Release\mdviewer.exe .\test-docs\mermaid.md
```

Linux, after checking out this branch and installing the existing dependencies:

```bash
./build.sh --skip-skia   # omit --skip-skia on the first Skia build
cmake --build build --target mdviewer_tests --parallel 2
ctest --test-dir build --output-on-failure
./build/mdviewer test-docs/mermaid.md
```

The CMake build invokes Cargo automatically with `--locked --release`. Its
artifacts live inside the selected build directory, in `mermaid-rust/`.
The bridge uses an optimized Rust build in both C++ configurations. On Windows
its CRT is statically linked to match the native application.

To compare against a build with no Rust dependency, use `-DisableMermaid` in
`build.ps1`, `--disable-mermaid` in `build.sh`, or configure CMake with
`-DMDVIEWER_ENABLE_MERMAID=OFF`. Such builds keep Mermaid source visible.
The build scripts reset this option from their own flags on each invocation.

## Implementation

- `third_party/mermaid_bridge/`: the Rust static library and small C ABI. Input
  and output use length-delimited UTF-8 buffers; Rust frees its own allocations.
  Rust panics are caught at the boundary and become source-fallback errors.
- `cmake/mermaid.cmake`: native Cargo build and platform system-library linkage.
- `src/render/mermaid_renderer.*`: a bounded cache and SVG DOM ownership.
  Both palettes share one computed layout, so changing themes does not invoke
  Rust again. Reader zoom and column fitting apply a vector transform.
- `src/render/mermaid_svg.*`: expands SVG arrow markers into ordinary paths,
  since the pinned Skia SVG implementation does not render markers. This uses
  the existing tinyxml2 and Skia path libraries. Diagram hyperlinks are passive.
- Shared block layout retains each diagram's original source in the document
  text. Hit testing and search treat the displayed diagram as one atomic run;
  a match within its source highlights the diagram. Copy buttons return source.

No platform-specific diagram rendering is needed. Normal Windows and Linux
hosts, PDF export and printing all use the same code.

## Open and save diagrams

Click a rendered diagram to open a full-size SVG in the system's default SVG
application. Depending on the file association, that may be an image viewer,
vector editor or browser. Dragging still selects text instead of opening the
diagram. The code-block copy button still copies Mermaid source.

Right-click a diagram for **Open Diagram** or **Save Diagram as SVG...**. The
native save dialog suggests `<document>-diagram.svg` and confirms overwriting.
The exported image uses the intrinsic vector dimensions, independent of reader
zoom and column width. It includes expanded arrowheads and the current light,
dark or sepia panel background, so dark labels remain readable in other apps.
It references installed fonts rather than embedding font files.

Preview SVGs are written to unique private directories under the system temp
directory. They remain there after the viewer closes so external applications
can continue reading them; normal temporary-file cleanup can remove them later.
Saving uses the destination chosen in the dialog. Source-fallback blocks do
not offer image actions. No extra library is required for these actions.

## Compatibility and limits

This is an independent implementation, not mermaid.js. Layout and syntax support
can differ. Flowcharts, sequence, class, state, ER and pie diagrams are included
in the manual fixture. Other upstream diagram types may work, but are not all
validated by this experiment. Font availability affects measured labels and
Unicode coverage; install suitable system fonts when testing Linux.

Rendering is synchronous during document layout and cached across resize/zoom.
There is no hard wall-clock cancellation in this first implementation.
Limits bound each input to 16 KiB, 256 lines, 128 graph nodes and 256 graph edges;
at most 64 Mermaid blocks per document are attempted. Each SVG is limited to
2 MiB and intrinsic dimensions to 16384 per axis. The cache retains at most
64 entries / 16 MiB of source-and-SVG payload (DOM overhead is additional).
Diagrams owned by the currently displayed layout remain alive after eviction.

Document-controlled configuration/front matter is not supported. SVG requiring
embedded HTML, raster images, style sheets or mid-edge markers falls back to
source with an explanation. This includes some C4 diagrams. Explicit supported
node/edge styling can still be used. The renderer loads no external SVG resources.

## Validation

The shared tests cover visible arrowhead pixels, flowchart/sequence rendering,
source offsets and copying, search, atomic hit testing, width/zoom scaling,
cache reuse, both themes, invalid/configured/oversized inputs, graph/document
limits and PDF output. Export tests cover click-versus-drag behavior, duplicate
diagrams, context-menu targeting, Unicode filenames, write failures, standalone
SVG round-trips, original dimensions and theme backgrounds. External application
launch and native save dialogs require manual testing on each host.
`test-docs/mermaid.md` exercises the GUI cases.

Set `MDVIEWER_TEST_ARTIFACT_DIR` to save light/dark PPM previews from the tests.
Linux C++ ASan/UBSan checks still run; the Rust library itself is not sanitizer
instrumented by those CMake flags. Run its own tests separately with:

```text
cargo test --locked --manifest-path third_party/mermaid_bridge/Cargo.toml --target-dir build/mermaid-rust-tests
```

Windows and Linux builds and rendering are validated locally. The Linux pass
on 2026-10-29 used Arch Linux x86_64, GCC 16.2.1 and Rust 1.98.1 with Mermaid
and PDF enabled. Shared tests, the performance regression runner, Rust tests
and C++ ASan/UBSan tests pass. The X11 application and unpacked release archive
were checked for light/dark diagrams, source search, zoom, resize, scrolling,
native GTK SVG saving and normal window closure. Shared tests also cover
Mermaid PDF output. External SVG application associations and the native print
dialog still need manual host checks.

The sanitizer pass exposed an out-of-bounds lookup in MicroTeX's font tables,
triggered by the existing accented/scalable-delimiter formula fixture. CMake
applies a bounded binary-search patch to fresh and cached dependency sources;
regressions cover empty, single-row and composite-key font tables.

The first Windows x64 Release comparison against `1c018b0` grew the executable
from 30,689,280 to approximately 36,044,000 bytes (about 5.1 MiB extra). This is
the executable size, not a runtime memory measurement. The first optimized
Cargo dependency build took about three minutes on the development machine;
unchanged builds reuse those artifacts. The locked graph has 34 external Rust
packages, including build-time and platform-specific dependencies.

## Updating the dependency

Change the exact Cargo version deliberately, regenerate `Cargo.lock`, and check
the generated SVG subset and marker fixtures again. Refresh license texts using
`python tools/update-mermaid-notices.py` with the release Rust toolchain active.
Release packaging includes the dependency and Rust standard-library notices.
