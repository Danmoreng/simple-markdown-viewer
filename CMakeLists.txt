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

# Skia - Using aseprite/skia fork as it is more CMake-friendly
# Note: Building Skia from source is still very slow.
FetchContent_Declare(
    skia
    GIT_REPOSITORY https://github.com/aseprite/skia.git
    GIT_TAG        m103-34e4029194 # A known stable tag for aseprite
)
# Skia options
set(SKIA_LIBRARY_KIND "static" CACHE STRING "" FORCE)
set(SKIA_BACKEND "raster" CACHE STRING "" FORCE) # Start with raster for simplicity
FetchContent_MakeAvailable(skia)

# Source files
set(SOURCES
    src/main.cpp
    src/platform/win/win_main.cpp
)

# Headers
set(HEADERS
    # Add headers here later
)

add_executable(mdviewer WIN32 ${SOURCES} ${HEADERS})

target_include_directories(mdviewer PRIVATE src)
target_link_libraries(mdviewer PRIVATE md4c)

# Basic Windows flags
if(MSVC)
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8)
endif()
