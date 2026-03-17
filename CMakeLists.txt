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
set(SKIA_DIR "${CMAKE_CURRENT_SOURCE_MACHINE_DIR}/third_party/skia" CACHE PATH "Path to Skia source")
set(SKIA_OUT_DIR "${SKIA_DIR}/out/Static" CACHE PATH "Path to Skia build output")

# Source files
set(SOURCES
    src/main.cpp
    src/platform/win/win_main.cpp
    src/util/file_io.cpp
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
)

target_link_libraries(mdviewer PRIVATE 
    md4c
    ${SKIA_OUT_DIR}/skia.lib
    # Windows dependencies for Skia
    windowscodecs.lib
    usp10.lib
    ole32.lib
    user32.lib
    gdi32.lib
)

# Basic Windows flags
if(MSVC)
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8)
    # Match Skia's static runtime
    set_property(TARGET mdviewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET md4c PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
