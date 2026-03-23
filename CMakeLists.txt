cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.1.2 LANGUAGES CXX)

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

# GLFW for Linux windowing
if(UNIX AND NOT APPLE)
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.3.8
    )
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # Use pkg-config to find GTK3 for native file dialogs
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
endif()

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
    src/util/file_io.cpp
    src/util/skia_font_utils.cpp
    src/markdown/markdown_parser.cpp
    src/layout/layout_engine.cpp
    src/render/document_renderer.cpp
    src/render/document_typefaces.cpp
    src/render/image_cache.cpp
    src/render/menu_renderer.cpp
    src/render/theme.cpp
    src/render/typography.cpp
    src/view/document_hit_test.cpp
    src/view/document_interaction.cpp
)

if(WIN32)
    list(APPEND SOURCES
        src/platform/win/win_clipboard.cpp
        src/platform/win/win_file_dialog.cpp
        src/platform/win/win_file_watcher.cpp
        src/platform/win/win_app.cpp
        src/platform/win/win_interaction.cpp
        src/platform/win/win_main.cpp
        src/platform/win/win_menu.cpp
        src/platform/win/win_shell.cpp
        src/platform/win/win_surface.cpp
        src/platform/win/win_viewer_host.cpp
        src/platform/win/win_window.cpp
        resources/app.rc
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND SOURCES
        src/platform/linux/linux_app.cpp
        src/platform/linux/linux_clipboard.cpp
        src/platform/linux/linux_file_dialog.cpp
        src/platform/linux/linux_font_dialog.cpp
        src/platform/linux/linux_interaction.cpp
        src/platform/linux/linux_main.cpp
        src/platform/linux/linux_menu.cpp
        src/platform/linux/linux_shell.cpp
        src/platform/linux/linux_surface.cpp
        src/platform/linux/linux_viewer_host.cpp
    )
endif()

# Headers
set(HEADERS
    # Add headers here later
)

if(WIN32)
    add_executable(mdviewer WIN32 ${SOURCES} ${HEADERS})
else()
    add_executable(mdviewer ${SOURCES} ${HEADERS})
endif()

target_include_directories(mdviewer PRIVATE 
    src
    ${SKIA_DIR}
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/config
    ${SKIA_DIR}/include/ports
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/include/gpu/ganesh
    ${SKIA_DIR}/include/gpu/ganesh/gl
    ${md4c_SOURCE_DIR}/src
)

if(UNIX AND NOT APPLE)
    target_include_directories(mdviewer PRIVATE ${GTK3_INCLUDE_DIRS})
endif()

if(WIN32)
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
else()
    target_link_libraries(mdviewer PRIVATE 
        md4c
        glfw
        ${GTK3_LIBRARIES}
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskia.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskia.a>
        # Linux dependencies for Skia
        fontconfig
        freetype
        pthread
        dl
        GL
        X11
    )
endif()

# Basic Windows flags
if(MSVC)
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267)
    target_compile_definitions(mdviewer PRIVATE UNICODE _UNICODE)
    # Match Skia's static runtime
    set_property(TARGET mdviewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET md4c PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
