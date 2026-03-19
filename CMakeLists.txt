cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# md4c - Lightweight Markdown parser
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
)
FetchContent_MakeAvailable(md4c)

# Skia - We will build this using official GN/Ninja tools via build.ps1
# The build script will place Skia in third_party/skia
set(SKIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/skia" CACHE PATH "Path to Skia source")
set(SKIA_OUT_DIR "${SKIA_DIR}/out/Static" CACHE PATH "Path to the non-Debug Skia build output")
set(SKIA_DEBUG_OUT_DIR "${SKIA_DIR}/out/Debug" CACHE PATH "Path to the Debug Skia build output")

# Source files
set(SOURCES
    src/main.cpp
    src/app/app_config.cpp
    src/app/document_loader.cpp
    src/app/link_resolver.cpp
    src/app/viewer_controller.cpp
    src/platform/win/win_clipboard.cpp
    src/platform/win/win_file_dialog.cpp
    src/platform/win/win_main.cpp
    src/platform/win/win_menu.cpp
    src/platform/win/win_shell.cpp
    src/platform/win/win_surface.cpp
    src/platform/win/win_window.cpp
    src/util/file_io.cpp
    src/util/skia_font_utils.cpp
    src/markdown/markdown_parser.cpp
    src/layout/layout_engine.cpp
    src/render/document_renderer.cpp
    src/render/theme.cpp
    src/render/typography.cpp
    src/view/document_hit_test.cpp
    src/view/document_interaction.cpp
    resources/app.rc
)

# Headers
set(HEADERS
    # Add headers here later
)

add_executable(mdviewer WIN32 ${SOURCES} ${HEADERS})

target_include_directories(mdviewer PRIVATE 
    src
    ${SKIA_DIR}
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/config
    ${SKIA_DIR}/include/ports
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(mdviewer PRIVATE 
    md4c
    $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skia.lib>
    $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skia.lib>
    # Windows dependencies for Skia
    windowscodecs.lib
    dwrite.lib
    usp10.lib
    ole32.lib
    user32.lib
    gdi32.lib
)

# Basic Windows flags
if(MSVC)
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267)
    target_compile_definitions(mdviewer PRIVATE UNICODE _UNICODE)
    # Match Skia's static runtime
    set_property(TARGET mdviewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET md4c PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
