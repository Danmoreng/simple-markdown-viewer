#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="build"
DIST_DIR="dist"
PACKAGE_NAME="mdviewer-linux-x64"
SKIP_BUILD=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=true ;;
        *) echo "Unknown parameter: $1" >&2; exit 1 ;;
    esac
    shift
done

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "Linux packaging must run on Linux." >&2
    exit 1
fi
if [[ "$(uname -m)" != "x86_64" ]]; then
    echo "This package target currently supports x86_64 only." >&2
    exit 1
fi

if [[ "$SKIP_BUILD" == false ]]; then
    if [[ -f third_party/skia/out/Static/libskia.a ]]; then
        ./build.sh --skip-skia
    else
        ./build.sh
    fi
fi

BINARY="$BUILD_DIR/mdviewer"
if [[ ! -x "$BINARY" ]]; then
    echo "Release executable not found: $BINARY" >&2
    exit 1
fi

BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$BUILD_DIR/CMakeCache.txt" | head -n1)"
SANITIZERS="$(sed -n 's/^MDVIEWER_ENABLE_SANITIZERS:BOOL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n1)"
if [[ "$BUILD_TYPE" != "Release" ]]; then
    echo "Refusing to package non-Release build: CMAKE_BUILD_TYPE=$BUILD_TYPE" >&2
    exit 1
fi
if [[ "${SANITIZERS,,}" != "off" && "${SANITIZERS,,}" != "false" ]]; then
    echo "Refusing to package a sanitizer-enabled build." >&2
    exit 1
fi

RUNTIME_DEPS="$(ldd "$BINARY")"
if grep -Fq "$PWD/$BUILD_DIR/" <<<"$RUNTIME_DEPS"; then
    echo "Refusing to package an executable that loads libraries from the build tree:" >&2
    grep -F "$PWD/$BUILD_DIR/" <<<"$RUNTIME_DEPS" >&2
    exit 1
fi
if grep -q 'not found' <<<"$RUNTIME_DEPS"; then
    echo "Refusing to package an executable with unresolved runtime libraries:" >&2
    grep 'not found' <<<"$RUNTIME_DEPS" >&2
    exit 1
fi

STAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ARCHIVE="$DIST_DIR/$PACKAGE_NAME.tar.gz"
rm -rf "$STAGE_DIR" "$ARCHIVE" "$ARCHIVE.sha256"
mkdir -p \
    "$STAGE_DIR/bin" \
    "$STAGE_DIR/share/applications" \
    "$STAGE_DIR/share/icons/hicolor/128x128/apps" \
    "$STAGE_DIR/share/pixmaps"

cp "$BINARY" "$STAGE_DIR/bin/mdviewer"
strip --strip-unneeded "$STAGE_DIR/bin/mdviewer"
cp LICENSE THIRD_PARTY_NOTICES README.md "$STAGE_DIR/"
cp resources/linux/mdviewer.desktop "$STAGE_DIR/share/applications/"
cp resources/linux/mdviewer.png "$STAGE_DIR/share/icons/hicolor/128x128/apps/mdviewer.png"
cp resources/linux/mdviewer.png "$STAGE_DIR/share/pixmaps/mdviewer.png"

cat > "$STAGE_DIR/run-mdviewer.sh" <<'RUNNER'
#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$ROOT/bin/mdviewer" "$@"
RUNNER
chmod +x "$STAGE_DIR/run-mdviewer.sh"

SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}"
tar \
    --sort=name \
    --mtime="@$SOURCE_DATE_EPOCH" \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    -C "$DIST_DIR" \
    -cf - "$PACKAGE_NAME" | gzip -n > "$ARCHIVE"
sha256sum "$ARCHIVE" > "$ARCHIVE.sha256"

echo "Created $ARCHIVE"
echo "SHA-256: $(cut -d' ' -f1 "$ARCHIVE.sha256")"
echo "Runtime dependencies:"
ldd "$STAGE_DIR/bin/mdviewer"
