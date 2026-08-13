# Linux Release and Smoke-Test Checklist

Use this checklist before publishing a Linux archive. Run it from the repository
root on an X11 or Wayland desktop with working OpenGL acceleration.

## 1. Clean release build

```bash
./build.sh --clean --skip-skia
cmake --build build --target mdviewer_tests --parallel 2
ctest --test-dir build --output-on-failure
```

Omit `--skip-skia` when the PDF-enabled Release Skia archive has not already
been built.

Verify the configuration:

```bash
grep -E '^(CMAKE_BUILD_TYPE|MDVIEWER_ENABLE_PDF|MDVIEWER_ENABLE_SANITIZERS):' build/CMakeCache.txt
```

Expected: `Release`, PDF enabled, sanitizers disabled.

## 2. Build and inspect the archive

```bash
./package-linux.sh --skip-build
file dist/mdviewer-linux-x64/bin/mdviewer
ldd dist/mdviewer-linux-x64/bin/mdviewer
sha256sum --check dist/mdviewer-linux-x64.tar.gz.sha256
```

The staged executable must be stripped. `ldd` must not contain paths below the
repository `build/` directory and must not report missing libraries. md4c,
utf8proc, GLFW, Tree-sitter, and Skia are linked into the executable; GTK3, OpenGL, X11,
fontconfig, freetype, libc, and other normal distribution libraries remain
system dependencies.

Test the archive from outside the repository:

```bash
rm -rf /tmp/mdviewer-release-test
mkdir /tmp/mdviewer-release-test
tar -xzf dist/mdviewer-linux-x64.tar.gz -C /tmp/mdviewer-release-test
/tmp/mdviewer-release-test/mdviewer-linux-x64/run-mdviewer.sh README.md
```

## 3. Manual viewer checks

Use `test-docs/markdown-rendering-fixture.md`,
`test-docs/html-and-table-overflow.md`, `test-docs/bidi-complex-text.md`, this
repository's `README.md`, and at least one real document containing relative
links and local images. When available, the gem16 README is the reference
fixture for the GitHub-style HTML header, `[!IMPORTANT]` alert, and ordinary
soft-break word wrapping.

- [ ] Start with no file and open a document through `File -> Open...`.
- [ ] Start with a path argument containing spaces.
- [ ] Drop a Markdown file onto the window.
- [ ] Scroll from start to end with the wheel; verify smooth rendering.
- [ ] Verify aligned HTML paragraphs/headings, requested local image sizes, and labeled placeholders for remote images.
- [ ] Verify `NOTE`, `TIP`, `IMPORTANT`, `WARNING`, and `CAUTION` render with distinct accents, titles, and icons.
- [ ] Shrink the window and verify long unbroken tokens remain inside the document viewport.
- [ ] Scroll a wide table horizontally using its thumb, Shift+wheel, and touchpad input where available; verify links and selection remain aligned after scrolling.
- [ ] Scroll a long code block horizontally, move the pointer out of the block, and verify the app remains stable.
- [ ] Select ASCII and non-ASCII text; drag rapidly and verify selection remains responsive.
- [ ] Copy selected text with `Ctrl+C` and verify it in another application.
- [ ] Open search with `Ctrl+F`; test next, previous, backspace, and Escape.
- [ ] Open a real relative Markdown link and use Back/Forward to return.
- [ ] Open an HTTPS link and verify the default browser receives the exact URL.
- [ ] Right-click selected text, links, images, tables, and document background; verify the menu opens without blocking and all enabled context-menu actions work.
- [ ] Click directly on a linked image, then click the free space beside it; verify only the image bounds activate the link or expose image/link context actions.
- [ ] Start middle-button auto-scroll, vary its speed above and below the origin, then stop it with middle-click, left-click, wheel, and Escape.
- [ ] Toggle and navigate the outline on both the left and right sides.
- [ ] In the BiDi fixture, verify Arabic joining/diacritics, Hebrew punctuation,
      mixed English/numbers, right-aligned RTL paragraphs, and logical copy order.
- [ ] Verify RTL bullets, numbers, task boxes, nested indentation, blockquote and
      alert accents, details summaries, table cells, and Arabic/Hebrew outline labels.
- [ ] Search exact Arabic and Hebrew terms, drag selection across direction
      boundaries, and click both visual ends of a mixed-direction link.
- [ ] Horizontally scroll the long mixed-direction code lines and verify source
      order, syntax colors, copy controls, and scrollbar behavior remain intact.
- [ ] Change Light, Sepia, and Dark themes.
- [ ] Select a document font, restore the default, and restart to verify persistence.
- [ ] Zoom with toolbar, keyboard, and Ctrl+wheel; verify reading position is preserved.
- [ ] Export the BiDi fixture to PDF and verify Arabic joining, Hebrew order,
      nested code, tables, math, and page breaks.
- [ ] Print the BiDi fixture through the GTK system dialog and verify the same
      shaped text, code, images, and page order in the output.
- [ ] Open menus with F10 and Alt+F/Alt+V/Alt+T; navigate with arrows, Enter, Escape, and recent-file number keys.
- [ ] Verify menu checkmarks, disabled commands, shortcut labels, the Theme submenu, and recent-file timestamps.
- [ ] Resize repeatedly and move the window between displays with different scale factors; verify resize work does not continue after the drag ends.
- [ ] Drag the outline width rapidly in both directions; verify it remains responsive, settles immediately on release, and persists after restart.
- [ ] Close the app normally and verify no terminal error is printed.

## 4. Known desktop integration boundary

The archive includes a desktop entry and icon under `share/`, but does not
install them automatically. A future distro package or installer should copy
those files into the appropriate system or per-user XDG locations and register
file associations according to the target distribution's policy.
