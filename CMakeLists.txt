cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Basic Windows flags
if(MSVC)
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8)
endif()
