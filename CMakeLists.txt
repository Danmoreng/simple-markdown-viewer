cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# md4c - Lightweight Markdown parser
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        v0.5.2
)
FetchContent_MakeAvailable(md4c)

# Skia - This is more complex. Building from source via FetchContent is VERY slow.
# For a v1 prototype, we might want to use a pre-built version or a minimal subset.
# However, if the user requested FetchContent, I will attempt to set up a way to fetch 
# a pre-built binary if possible, or build a minimal version.
# For now, let's stick to the structure and I'll look into a manageable Skia setup.

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
