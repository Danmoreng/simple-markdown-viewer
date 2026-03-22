#!/bin/bash
set -e

CONFIGURATION="Release"
BUILD_DIR="build"
TARGET="mdviewer"
SKIP_SKIA=false

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --clean) CLEAN=true ;;
        --debug) CONFIGURATION="Debug" ;;
        --skip-skia) SKIP_SKIA=true ;;
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
    GN_ARGS="is_official_build=$IS_OFFICIAL is_debug=$IS_DEBUG skia_use_system_libpng=false skia_use_system_libwebp=false skia_use_system_libjpeg_turbo=false skia_use_system_zlib=false skia_use_system_icu=false skia_use_system_harfbuzz=false skia_use_expat=false skia_use_libpng_encode=false skia_use_libjpeg_turbo_encode=false skia_use_libwebp_encode=false skia_use_vulkan=false skia_use_metal=false skia_enable_pdf=false skia_enable_skottie=false skia_use_icu=false skia_enable_skshaper=false skia_enable_svg=false skia_use_piex=false"
    
    # GN and Ninja paths
    GN_PATH="./bin/gn"
    NINJA_PATH="$DEPOT_TOOLS_DIR/ninja"

    $GN_PATH gen "$SKIA_OUT_DIR" --args="$GN_ARGS"

    echo "Building Skia..."
    $NINJA_PATH -C "$SKIA_OUT_DIR" skia
    popd
fi

SKIA_OUT_PATH="$SKIA_DIR/out/Static"
if [ "$CONFIGURATION" = "Debug" ]; then
    SKIA_OUT_PATH="$SKIA_DIR/out/Debug"
fi

echo "Configuring CMake..."
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CONFIGURATION" \
    -DSKIA_DIR="$SKIA_DIR" \
    -DSKIA_OUT_DIR="$SKIA_OUT_PATH" \
    -DSKIA_DEBUG_OUT_DIR="$SKIA_DIR/out/Debug"

echo "Building target $TARGET ($CONFIGURATION)..."
cmake --build "$BUILD_DIR" --target "$TARGET"
