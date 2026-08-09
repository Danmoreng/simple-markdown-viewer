#!/bin/bash
set -e

CONFIGURATION="Release"
BUILD_DIR="build"
TARGET="mdviewer"
SKIP_SKIA=false
ENABLE_PDF=true

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --clean) CLEAN=true ;;
        --debug) CONFIGURATION="Debug" ;;
        --skip-skia) SKIP_SKIA=true ;;
        --disable-pdf) ENABLE_PDF=false ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# Handle Skia and depot_tools
THIRD_PARTY_DIR="$(pwd)/third_party"
SKIA_DIR="$THIRD_PARTY_DIR/skia"
DEPOT_TOOLS_DIR="$THIRD_PARTY_DIR/depot_tools"
SKIA_REVISION_FILE="$(pwd)/ci/skia-revision.txt"

if [ ! -f "$SKIA_REVISION_FILE" ]; then
    echo "Missing Skia revision file: $SKIA_REVISION_FILE" >&2
    exit 1
fi
SKIA_REVISION="$(tr -d '[:space:]' < "$SKIA_REVISION_FILE")"
if [[ ! "$SKIA_REVISION" =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "Invalid Skia revision in $SKIA_REVISION_FILE" >&2
    exit 1
fi

mkdir -p "$THIRD_PARTY_DIR"

if [ "$SKIP_SKIA" = false ]; then
    if [ ! -d "$DEPOT_TOOLS_DIR" ]; then
        echo "Cloning depot_tools..."
        git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_DIR"
    fi

    export PATH="$DEPOT_TOOLS_DIR:$PATH"

    if [ ! -d "$SKIA_DIR" ]; then
        echo "Cloning official Skia repository..."
        git clone https://skia.googlesource.com/skia.git "$SKIA_DIR"
    fi

    pushd "$SKIA_DIR"
    echo "Checking out pinned Skia revision $SKIA_REVISION..."
    git fetch origin "$SKIA_REVISION"
    git checkout --detach "$SKIA_REVISION"
    ACTUAL_SKIA_REVISION="$(git rev-parse HEAD)"
    if [ "$ACTUAL_SKIA_REVISION" != "$SKIA_REVISION" ]; then
        echo "Skia revision mismatch: expected $SKIA_REVISION, got $ACTUAL_SKIA_REVISION" >&2
        exit 1
    fi

    echo "Syncing Skia dependencies..."
    export GIT_SYNC_DEPS_SKIP_EMSDK=1
    python3 tools/git-sync-deps

    echo "Fetching GN and Ninja..."
    python3 bin/fetch-gn
    python3 bin/fetch-ninja

    SKIA_OUT_DIR="out/Static"
    IS_DEBUG="false"
    IS_OFFICIAL="true"
    if [ "$CONFIGURATION" = "Debug" ]; then
        SKIA_OUT_DIR="out/Debug"
        IS_DEBUG="true"
        IS_OFFICIAL="false"
    fi

    echo "Configuring Skia with GN ($CONFIGURATION)..."
    SKIA_ENABLE_PDF="$ENABLE_PDF"
    GN_ARGS="is_official_build=$IS_OFFICIAL is_debug=$IS_DEBUG skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_libjpeg_turbo=false skia_use_system_zlib=false skia_use_system_icu=false skia_use_system_harfbuzz=false skia_use_expat=true skia_use_system_expat=false skia_use_libpng_encode=false skia_use_libjpeg_turbo_encode=false skia_use_libwebp_encode=false skia_use_vulkan=false skia_use_metal=false skia_enable_pdf=$SKIA_ENABLE_PDF skia_pdf_subset_harfbuzz=false skia_enable_skottie=false skia_use_icu=false skia_enable_skshaper=true skia_enable_svg=true skia_use_piex=false"
    
    # GN and Ninja paths
    GN_PATH="./bin/gn"
    NINJA_PATH="$DEPOT_TOOLS_DIR/ninja"

    $GN_PATH gen "$SKIA_OUT_DIR" --args="$GN_ARGS"

    echo "Building Skia..."
    $NINJA_PATH -C "$SKIA_OUT_DIR" skia modules/svg:svg

    SKIA_MILESTONE="$(awk '/^#define SK_MILESTONE / { print $3; exit }' include/core/SkMilestone.h)"
    printf '%s\n' "$SKIA_REVISION" > "$SKIA_OUT_DIR/SKIA_REVISION"
    printf '%s\n' "$SKIA_MILESTONE" > "$SKIA_OUT_DIR/SKIA_MILESTONE"
    printf '%s\n' "$GN_ARGS" > "$SKIA_OUT_DIR/SKIA_GN_ARGS"
    echo "Built Skia revision $SKIA_REVISION (milestone $SKIA_MILESTONE)."
    popd
fi

SKIA_OUT_PATH="$SKIA_DIR/out/Static"
if [ "$CONFIGURATION" = "Debug" ]; then
    SKIA_OUT_PATH="$SKIA_DIR/out/Debug"
fi

if [ "$ENABLE_PDF" = true ]; then
    SKIA_ARGS_FILE="$SKIA_OUT_PATH/SKIA_GN_ARGS"
    if [ ! -f "$SKIA_ARGS_FILE" ] || ! grep -q 'skia_pdf_subset_harfbuzz=false' "$SKIA_ARGS_FILE"; then
        echo "The existing Skia build enables the unstable HarfBuzz PDF font subsetter." >&2
        echo "Rebuild Skia with ./build.sh before using PDF export; do not pass --skip-skia." >&2
        exit 1
    fi
fi

echo "Configuring CMake..."
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CONFIGURATION" \
    -DSKIA_DIR="$SKIA_DIR" \
    -DSKIA_OUT_DIR="$SKIA_OUT_PATH" \
    -DSKIA_DEBUG_OUT_DIR="$SKIA_DIR/out/Debug" \
    -DMDVIEWER_ENABLE_PDF="$ENABLE_PDF"

echo "Building target $TARGET ($CONFIGURATION)..."
cmake --build "$BUILD_DIR" --target "$TARGET"
