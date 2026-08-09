cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.1.7 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
enable_testing()

option(MDVIEWER_ENABLE_PDF "Enable PDF export (requires a PDF-enabled Skia build)" ON)
option(MDVIEWER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Fetched runtime dependencies are linked statically so the Linux release does
# not depend on libraries inside the build tree.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build fetched dependencies as static libraries" FORCE)

# md4c - Lightweight Markdown parser
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
)
FetchContent_MakeAvailable(md4c)

# Tree-sitter - mature parser-based syntax highlighting for fenced code blocks.
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        4f7ab225840f05e080851cc839a1fa92bbf46a36
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_c
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
    GIT_TAG        b780e47fc780ddc8da13afa35a3f4ed5c157823d
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        8b5b49eb196bec7040441bee33b2c9a4838d6967
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG        58404d8cf191d69f2674a8fd507bd5776f46cb11
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG        75b3874edb2dc714fb1fd77a32013d0f8699989f
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_json
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-json.git
    GIT_TAG        001c28d7a29832b06b0e831ec77845553c89b56d
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG        26855eabccb19c6abf499fbc5b8dc7cc9ab8bc64
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_bash
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-bash.git
    GIT_TAG        a06c2e4415e9bc0346c6b86d401879ffb44058f7
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_rust
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
    GIT_TAG        77a3747266f4d621d0757825e6b11edcbf991ca5
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
    GIT_TAG        2346a3ab1bb3857b48b29d779a1ef9799a248cd7
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_Declare(
    tree_sitter_c_sharp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c-sharp.git
    GIT_TAG        9150f7d56bb47f1a809fa23623f1ba1413e93fa9
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_MakeAvailable(
    tree_sitter
    tree_sitter_c
    tree_sitter_cpp
    tree_sitter_javascript
    tree_sitter_typescript
    tree_sitter_json
    tree_sitter_python
    tree_sitter_bash
    tree_sitter_rust
    tree_sitter_go
    tree_sitter_c_sharp
)

set(TREE_SITTER_CORE_SOURCE "${tree_sitter_SOURCE_DIR}/lib/src/lib.c")
set(TREE_SITTER_LANGUAGE_SOURCES
    "${tree_sitter_c_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_cpp_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_javascript_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_javascript_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_typescript_SOURCE_DIR}/typescript/src/parser.c"
    "${tree_sitter_typescript_SOURCE_DIR}/typescript/src/scanner.c"
    "${tree_sitter_typescript_SOURCE_DIR}/tsx/src/parser.c"
    "${tree_sitter_typescript_SOURCE_DIR}/tsx/src/scanner.c"
    "${tree_sitter_json_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_python_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_python_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_bash_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_bash_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_rust_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_go_SOURCE_DIR}/src/parser.c"
    "${tree_sitter_c_sharp_SOURCE_DIR}/src/parser.c"
)
if(EXISTS "${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c")
    list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c")
endif()
foreach(optional_scanner
    "${tree_sitter_rust_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_go_SOURCE_DIR}/src/scanner.c"
    "${tree_sitter_c_sharp_SOURCE_DIR}/src/scanner.c")
    if(EXISTS "${optional_scanner}")
        list(APPEND TREE_SITTER_LANGUAGE_SOURCES "${optional_scanner}")
    endif()
endforeach()
set(TREE_SITTER_QUERY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated_tree_sitter_queries.cpp")
file(WRITE "${TREE_SITTER_QUERY_SOURCE}"
    "#include \"render/syntax/tree_sitter_queries.h\"\n\n"
    "namespace mdviewer::syntax {\n\n")

function(append_tree_sitter_query function_name)
    set(query_text "")
    foreach(query_path IN ITEMS ${ARGN})
        if(EXISTS "${query_path}")
            file(READ "${query_path}" query_fragment)
            string(APPEND query_text "${query_fragment}\n")
        endif()
    endforeach()
    file(APPEND "${TREE_SITTER_QUERY_SOURCE}"
        "const char* ${function_name}() {\n"
        "    return R\"mdvquery(${query_text})mdvquery\";\n"
        "}\n\n")
endfunction()

append_tree_sitter_query(GetTreeSitterCQuery "${tree_sitter_c_SOURCE_DIR}/queries/highlights.scm")
# tree-sitter-cpp's highlights query contains only C++-specific additions and
# relies on the host query loader to inherit the base C highlights.
append_tree_sitter_query(GetTreeSitterCppQuery
    "${tree_sitter_c_SOURCE_DIR}/queries/highlights.scm"
    "${tree_sitter_cpp_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterJavaScriptQuery "${tree_sitter_javascript_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterTypeScriptQuery "${tree_sitter_typescript_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterTsxQuery "${tree_sitter_typescript_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterJsonQuery "${tree_sitter_json_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterPythonQuery "${tree_sitter_python_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterBashQuery "${tree_sitter_bash_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterRustQuery "${tree_sitter_rust_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterGoQuery "${tree_sitter_go_SOURCE_DIR}/queries/highlights.scm")
append_tree_sitter_query(GetTreeSitterCSharpQuery "${tree_sitter_c_sharp_SOURCE_DIR}/queries/highlights.scm")

file(APPEND "${TREE_SITTER_QUERY_SOURCE}" "} // namespace mdviewer::syntax\n")

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
    src/app/heading_anchor.cpp
    src/app/link_resolver.cpp
    src/app/viewer_controller.cpp
    src/util/file_io.cpp
    src/util/skia_font_utils.cpp
    src/util/utf8.cpp
    src/markdown/markdown_parser.cpp
    src/layout/layout_engine.cpp
    src/render/document_renderer.cpp
    src/render/document_typefaces.cpp
    src/render/image_cache.cpp
    src/render/menu_renderer.cpp
    src/render/pdf_exporter.cpp
    src/render/print_document.cpp
    src/render/syntax/tree_sitter_highlighter.cpp
    src/render/theme.cpp
    src/render/typography.cpp
    src/view/document_context_menu.cpp
    src/view/document_hit_test.cpp
    src/view/document_interaction.cpp
    src/view/document_outline.cpp
    ${TREE_SITTER_CORE_SOURCE}
    ${TREE_SITTER_LANGUAGE_SOURCES}
    ${TREE_SITTER_QUERY_SOURCE}
)

if(WIN32)
    list(APPEND SOURCES
        src/platform/win/win_clipboard.cpp
        src/platform/win/win_crash_handler.cpp
        src/platform/win/win_file_dialog.cpp
        src/platform/win/win_file_watcher.cpp
        src/platform/win/win_app.cpp
        src/platform/win/win_interaction.cpp
        src/platform/win/win_main.cpp
        src/platform/win/win_menu.cpp
        src/platform/win/win_context_menu.cpp
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
        src/platform/linux/linux_print.cpp
        src/platform/linux/linux_context_menu.cpp
        src/platform/linux/linux_shell.cpp
        src/platform/linux/linux_surface.cpp
        src/platform/linux/linux_viewer_host.cpp
        src/platform/linux/linux_window_icon.cpp
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

target_compile_definitions(mdviewer PRIVATE
    MDVIEWER_ENABLE_PDF=$<BOOL:${MDVIEWER_ENABLE_PDF}>)

target_include_directories(mdviewer PRIVATE 
    src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_SOURCE_DIR}/lib/src
    ${tree_sitter_c_SOURCE_DIR}/src
    ${tree_sitter_cpp_SOURCE_DIR}/src
    ${tree_sitter_javascript_SOURCE_DIR}/src
    ${tree_sitter_typescript_SOURCE_DIR}/typescript/src
    ${tree_sitter_typescript_SOURCE_DIR}/tsx/src
    ${tree_sitter_json_SOURCE_DIR}/src
    ${tree_sitter_python_SOURCE_DIR}/src
    ${tree_sitter_bash_SOURCE_DIR}/src
    ${tree_sitter_rust_SOURCE_DIR}/src
    ${tree_sitter_go_SOURCE_DIR}/src
    ${tree_sitter_c_sharp_SOURCE_DIR}/src
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
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/svg.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/svg.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skresources.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skresources.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skshaper.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skshaper.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skia.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skia.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/expat.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/expat.lib>
        # Windows dependencies for Skia
        windowscodecs.lib
        dwrite.lib
        usp10.lib
        ole32.lib
        dbghelp.lib
        user32.lib
        gdi32.lib
    )
else()
    target_link_libraries(mdviewer PRIVATE 
        md4c
        glfw
        ${GTK3_LIBRARIES}
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libsvg.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libsvg.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskresources.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskresources.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskshaper.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskshaper.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskia.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskia.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libexpat.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libexpat.a>
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
    target_compile_options(mdviewer PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267 /wd5030)
    set_source_files_properties(
        ${TREE_SITTER_CORE_SOURCE}
        ${TREE_SITTER_LANGUAGE_SOURCES}
        ${TREE_SITTER_QUERY_SOURCE}
        PROPERTIES COMPILE_OPTIONS /w)
    target_compile_definitions(mdviewer PRIVATE UNICODE _UNICODE)
    # Match Skia's static runtime
    set_property(TARGET mdviewer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET md4c PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(mdviewer_tests
    tests/unit_tests.cpp
    src/app/app_config.cpp
    src/app/document_loader.cpp
    src/app/heading_anchor.cpp
    src/app/link_resolver.cpp
    src/app/viewer_controller.cpp
    src/layout/layout_engine.cpp
    src/markdown/markdown_parser.cpp
    src/render/document_renderer.cpp
    src/render/image_cache.cpp
    src/render/menu_renderer.cpp
    src/render/pdf_exporter.cpp
    src/render/print_document.cpp
    src/render/syntax/tree_sitter_highlighter.cpp
    src/render/theme.cpp
    src/render/typography.cpp
    src/util/file_io.cpp
    src/util/skia_font_utils.cpp
    src/util/utf8.cpp
    src/view/document_hit_test.cpp
    src/view/document_interaction.cpp
    src/view/document_outline.cpp
    ${TREE_SITTER_CORE_SOURCE}
    ${TREE_SITTER_LANGUAGE_SOURCES}
    ${TREE_SITTER_QUERY_SOURCE}
)

target_compile_definitions(mdviewer_tests PRIVATE
    MDVIEWER_ENABLE_PDF=$<BOOL:${MDVIEWER_ENABLE_PDF}>)

target_include_directories(mdviewer_tests PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_SOURCE_DIR}/lib/src
    ${tree_sitter_c_SOURCE_DIR}/src
    ${tree_sitter_cpp_SOURCE_DIR}/src
    ${tree_sitter_javascript_SOURCE_DIR}/src
    ${tree_sitter_typescript_SOURCE_DIR}/typescript/src
    ${tree_sitter_typescript_SOURCE_DIR}/tsx/src
    ${tree_sitter_json_SOURCE_DIR}/src
    ${tree_sitter_python_SOURCE_DIR}/src
    ${tree_sitter_bash_SOURCE_DIR}/src
    ${tree_sitter_rust_SOURCE_DIR}/src
    ${tree_sitter_go_SOURCE_DIR}/src
    ${tree_sitter_c_sharp_SOURCE_DIR}/src
    ${SKIA_DIR}
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/config
    ${SKIA_DIR}/include/ports
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/include/gpu/ganesh
    ${SKIA_DIR}/include/gpu/ganesh/gl
    ${md4c_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(mdviewer_tests PRIVATE
        md4c
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/svg.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/svg.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skresources.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skresources.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skshaper.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skshaper.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/skia.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/skia.lib>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/expat.lib>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/expat.lib>
        windowscodecs.lib
        dwrite.lib
        usp10.lib
        ole32.lib
        user32.lib
        gdi32.lib
        comdlg32.lib
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(mdviewer_tests PRIVATE
        md4c
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libsvg.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libsvg.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskresources.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskresources.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskshaper.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskshaper.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libskia.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libskia.a>
        $<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/libexpat.a>
        $<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/libexpat.a>
        fontconfig
        freetype
        pthread
        dl
        GL
        X11
    )
else()
    target_link_libraries(mdviewer_tests PRIVATE md4c)
endif()

if(MSVC)
    target_compile_options(mdviewer_tests PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267 /wd5030)
    set_property(TARGET mdviewer_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MDVIEWER_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        foreach(target mdviewer mdviewer_tests)
            target_compile_options(${target} PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
                -fno-sanitize=vptr)
            target_link_options(${target} PRIVATE
                -fno-omit-frame-pointer
                -fsanitize=address,undefined
                -fno-sanitize=vptr)
        endforeach()
    else()
        message(FATAL_ERROR "MDVIEWER_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

add_test(NAME mdviewer_unit_tests COMMAND mdviewer_tests)
