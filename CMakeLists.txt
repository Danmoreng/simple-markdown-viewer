cmake_minimum_required(VERSION 3.20)
project(mdviewer VERSION 0.5.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
enable_testing()

# Existing checkouts can be configured without contacting every upstream. A
# dependency revision change that is not present locally needs this opt-in.
option(MDVIEWER_UPDATE_DEPENDENCIES "Fetch updates for populated dependency checkouts" OFF)
if(NOT MDVIEWER_UPDATE_DEPENDENCIES)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
endif()
option(MDVIEWER_BUILD_FUZZERS "Build Clang/libFuzzer parser and SVG fuzz targets" OFF)
option(MDVIEWER_BUILD_BENCHMARKS "Build the document performance regression runner" OFF)

option(MDVIEWER_ENABLE_PDF "Enable PDF export (requires a PDF-enabled Skia build)" ON)
option(MDVIEWER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(MDVIEWER_ENABLE_MERMAID "Enable native Mermaid diagrams (requires Rust/Cargo at build time)" ON)
if(MDVIEWER_ENABLE_MERMAID)
    include(cmake/mermaid.cmake)
endif()

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

# MicroTeX - Native TeX math layout. md4c recognizes the delimiters while
# MicroTeX turns the formula source into a platform-independent box tree.
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        9.0.0
)
FetchContent_Declare(
    microtex
    GIT_REPOSITORY https://github.com/NanoMichael/MicroTeX.git
    GIT_TAG        0e3707f6dafebb121d98b53c64364d16fefe481d
    SOURCE_SUBDIR  cmake-unused
)
FetchContent_MakeAvailable(tinyxml2 microtex)

# Apply fixes on configuration as well as on a fresh fetch so cached checkouts
# and FETCHCONTENT_SOURCE_DIR_MICROTEX overrides receive the same patches.
foreach(microtex_patch microtex-fix-linux-home-path microtex-fix-indexed-array-bounds)
    execute_process(COMMAND
        "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${microtex_SOURCE_DIR}"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/${microtex_patch}.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_git_patch.cmake"
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()

# The pinned dependency's source list changes only when we configure a new
# revision. Avoid recursively rescanning it on every incremental build.
file(GLOB_RECURSE MICROTEX_SOURCES
    "${microtex_SOURCE_DIR}/src/atom/*.cpp"
    "${microtex_SOURCE_DIR}/src/box/*.cpp"
    "${microtex_SOURCE_DIR}/src/core/*.cpp"
    "${microtex_SOURCE_DIR}/src/fonts/*.cpp"
    "${microtex_SOURCE_DIR}/src/res/*.cpp"
    "${microtex_SOURCE_DIR}/src/utils/*.cpp"
)
list(APPEND MICROTEX_SOURCES
    "${microtex_SOURCE_DIR}/src/latex.cpp"
    "${microtex_SOURCE_DIR}/src/render.cpp")
add_library(microtex STATIC ${MICROTEX_SOURCES})
target_include_directories(microtex PUBLIC "${microtex_SOURCE_DIR}/src")
target_link_libraries(microtex PRIVATE tinyxml2::tinyxml2)
target_compile_features(microtex PUBLIC cxx_std_17)

# utf8proc - Portable Unicode case conversion for GitHub-style heading anchors.
set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    utf8proc
    GIT_REPOSITORY https://github.com/JuliaStrings/utf8proc.git
    GIT_TAG        v2.11.3
)
FetchContent_MakeAvailable(utf8proc)

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
# Legacy C scanner definitions use () while the generated parser and core use
# (void). Give both sides the same prototype for Clang's function-type UBSan
# checks without disabling those checks on the Tree-sitter callback boundary.
foreach(scanner_source IN LISTS TREE_SITTER_LANGUAGE_SOURCES)
    if(scanner_source MATCHES "/scanner\\.c$")
        file(READ "${scanner_source}" scanner_original)
        string(REGEX REPLACE "_external_scanner_create\\(\\)([ \t\r\n]*\\{)"
            "_external_scanner_create(void)\\1" scanner_patched "${scanner_original}")
        if(NOT scanner_original STREQUAL scanner_patched)
            file(WRITE "${scanner_source}" "${scanner_patched}")
        endif()
    endif()
endforeach()
set(TREE_SITTER_QUERY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated_tree_sitter_queries.cpp")
set(TREE_SITTER_QUERY_STAGING "${TREE_SITTER_QUERY_SOURCE}.in")
file(WRITE "${TREE_SITTER_QUERY_STAGING}"
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
    file(APPEND "${TREE_SITTER_QUERY_STAGING}"
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

file(APPEND "${TREE_SITTER_QUERY_STAGING}" "} // namespace mdviewer::syntax\n")
configure_file("${TREE_SITTER_QUERY_STAGING}" "${TREE_SITTER_QUERY_SOURCE}" COPYONLY)

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

# Shared sources are compiled once for the application, tests and tools.
set(MDVIEWER_CORE_SOURCES
    src/app/app_config.cpp
    src/app/diagram_export.cpp
    src/app/document_loader.cpp
    src/app/heading_anchor.cpp
    src/app/link_resolver.cpp
    src/app/image_source.cpp
    src/app/viewer_controller.cpp
    src/util/file_io.cpp
    src/util/skia_font_utils.cpp
    src/util/utf8.cpp
    src/text/complex_text_runtime.cpp
    src/text/document_font_style.cpp
    src/text/shaped_text_layout.cpp
    src/text/text_geometry.cpp
    src/markdown/markdown_parser.cpp
    src/layout/layout_engine.cpp
    src/render/document_renderer.cpp
    src/render/document_typefaces.cpp
    src/render/image_cache.cpp
    src/render/menu_renderer.cpp
    src/render/math_renderer.cpp
    src/render/mermaid_renderer.cpp
    src/render/mermaid_svg.cpp
    src/render/microtex_skia_adapter.cpp
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

set(MDVIEWER_HOST_SOURCES)
if(WIN32)
    list(APPEND MDVIEWER_HOST_SOURCES
        src/platform/win/win_clipboard.cpp
        src/platform/win/win_crash_handler.cpp
        src/platform/win/win_file_dialog.cpp
        src/platform/win/win_file_watcher.cpp
        src/platform/win/win_app.cpp
        src/platform/win/win_interaction.cpp
        src/platform/win/win_main.cpp
        src/platform/win/win_menu.cpp
        src/platform/win/win_message_dialog.cpp
        src/platform/win/win_context_menu.cpp
        src/platform/win/win_shell.cpp
        src/platform/win/win_surface.cpp
        src/platform/win/win_viewer_host.cpp
        src/platform/win/win_window.cpp
        resources/app.rc
    )
elseif(UNIX AND NOT APPLE)
    list(APPEND MDVIEWER_HOST_SOURCES
        src/platform/linux/linux_app.cpp
        src/platform/linux/linux_clipboard.cpp
        src/platform/linux/linux_file_dialog.cpp
        src/platform/linux/linux_font_dialog.cpp
        src/platform/linux/linux_interaction.cpp
        src/platform/linux/linux_main.cpp
        src/platform/linux/linux_menu.cpp
        src/platform/linux/linux_message_dialog.cpp
        src/platform/linux/linux_print.cpp
        src/platform/linux/linux_context_menu.cpp
        src/platform/linux/linux_shell.cpp
        src/platform/linux/linux_surface.cpp
        src/platform/linux/linux_viewer_host.cpp
        src/platform/linux/linux_window_icon.cpp
    )
endif()

if(WIN32)
    list(APPEND MDVIEWER_CORE_SOURCES src/platform/win/win_image_download.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND MDVIEWER_CORE_SOURCES src/platform/linux/linux_image_download.cpp)
else()
    message(FATAL_ERROR "Supported hosts are Windows and Linux")
endif()
add_library(mdviewer_core STATIC ${MDVIEWER_CORE_SOURCES})
target_compile_definitions(mdviewer_core PUBLIC
    MDVIEWER_ENABLE_MERMAID=$<BOOL:${MDVIEWER_ENABLE_MERMAID}>
    MDVIEWER_ENABLE_PDF=$<BOOL:${MDVIEWER_ENABLE_PDF}>)
target_include_directories(mdviewer_core PUBLIC
    src
    ${SKIA_DIR}
    ${SKIA_DIR}/include/core
    ${SKIA_DIR}/include/config
    ${SKIA_DIR}/include/ports
    ${SKIA_DIR}/include/gpu
    ${SKIA_DIR}/include/gpu/ganesh
    ${SKIA_DIR}/include/gpu/ganesh/d3d
    ${SKIA_DIR}/include/gpu/ganesh/gl
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${md4c_SOURCE_DIR}/src
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
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
    ${tree_sitter_c_sharp_SOURCE_DIR}/src)
target_link_libraries(mdviewer_core PUBLIC md4c utf8proc microtex tinyxml2::tinyxml2)
if(MDVIEWER_ENABLE_MERMAID)
    target_link_libraries(mdviewer_core PUBLIC mdviewer_mermaid)
endif()

# Preserve archive order: SVG/shaping consumers precede the Skia/ICU providers.
if(WIN32)
    set(skia_prefix "")
    set(skia_suffix ".lib")
else()
    set(skia_prefix "lib")
    set(skia_suffix ".a")
endif()
foreach(library svg skresources skshaper skunicode_icu skunicode_core harfbuzz icu skia expat)
    target_link_libraries(mdviewer_core PUBLIC
        "$<$<CONFIG:Debug>:${SKIA_DEBUG_OUT_DIR}/${skia_prefix}${library}${skia_suffix}>"
        "$<$<NOT:$<CONFIG:Debug>>:${SKIA_OUT_DIR}/${skia_prefix}${library}${skia_suffix}>")
endforeach()
if(WIN32)
    target_compile_definitions(mdviewer_core PUBLIC UNICODE _UNICODE)
    target_link_libraries(mdviewer_core PUBLIC
        windowscodecs dwrite usp10 ole32 user32 gdi32 advapi32 winhttp)
else()
    find_package(CURL REQUIRED)
    target_link_libraries(mdviewer_core PUBLIC
        CURL::libcurl fontconfig freetype pthread ${CMAKE_DL_LIBS} GL X11)
endif()

if(WIN32)
    add_executable(mdviewer WIN32 src/main.cpp ${MDVIEWER_HOST_SOURCES})
    target_link_libraries(mdviewer PRIVATE mdviewer_core dbghelp comdlg32 shell32 d3d12 d3dcompiler dxgi)
else()
    add_executable(mdviewer src/main.cpp ${MDVIEWER_HOST_SOURCES})
    target_include_directories(mdviewer PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(mdviewer PRIVATE mdviewer_core glfw ${GTK3_LIBRARIES})
endif()

# Only the small first-party test directory is scanned on incremental builds.
file(GLOB MDVIEWER_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_tests.cpp")
add_executable(mdviewer_tests ${MDVIEWER_TEST_SOURCES})
target_link_libraries(mdviewer_tests PRIVATE mdviewer_core)
if(WIN32)
    target_link_libraries(mdviewer_tests PRIVATE comdlg32)
endif()

# All executables use one per-configuration runtime directory. One shared target
# owns resources; parallel app/test builds never remove each other's files.
if(CMAKE_CONFIGURATION_TYPES)
    set(MDVIEWER_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
else()
    set(MDVIEWER_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()
add_custom_target(mdviewer_runtime_resources
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MDVIEWER_RUNTIME_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${microtex_SOURCE_DIR}/res/fonts" "${MDVIEWER_RUNTIME_DIR}/res/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${microtex_SOURCE_DIR}/res/.clatexmath-res_root"
        "${microtex_SOURCE_DIR}/res/RES_README" "${MDVIEWER_RUNTIME_DIR}/res"
    COMMENT "Staging shared runtime resources"
    VERBATIM)
if(WIN32)
    set(SKIA_ICU_DATA_FILE
        "$<IF:$<CONFIG:Debug>,${SKIA_DEBUG_OUT_DIR}/icudtl.dat,${SKIA_OUT_DIR}/icudtl.dat>")
    add_custom_command(TARGET mdviewer_runtime_resources POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_FILE=${SKIA_ICU_DATA_FILE}"
            "-DDESTINATION_FILE=${MDVIEWER_RUNTIME_DIR}/icudtl.dat"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_required_file.cmake"
        VERBATIM)
endif()

set(mdviewer_executables mdviewer mdviewer_tests)
if(MDVIEWER_BUILD_BENCHMARKS)
    add_executable(mdviewer_benchmarks tools/benchmarks/document_benchmarks.cpp)
    target_link_libraries(mdviewer_benchmarks PRIVATE mdviewer_core)
    list(APPEND mdviewer_executables mdviewer_benchmarks)
    add_test(NAME mdviewer_performance_regressions COMMAND mdviewer_benchmarks --check)
    set_tests_properties(mdviewer_performance_regressions PROPERTIES
        LABELS performance TIMEOUT 90 WORKING_DIRECTORY "${MDVIEWER_RUNTIME_DIR}")
endif()

set(mdviewer_instrumented_targets mdviewer_core md4c utf8proc tinyxml2 microtex)
if(MDVIEWER_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
        foreach(target IN LISTS mdviewer_instrumented_targets)
            target_compile_options(${target} PRIVATE
                -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=vptr)
        endforeach()
        # Static libraries need the sanitizer runtime in every final executable.
        target_link_options(mdviewer_core INTERFACE
            -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=vptr)
    else()
        message(FATAL_ERROR "MDVIEWER_ENABLE_SANITIZERS requires GCC or Clang on Linux")
    endif()
endif()

if(MDVIEWER_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR MSVC OR NOT MDVIEWER_ENABLE_SANITIZERS)
        message(FATAL_ERROR "Fuzz targets require Clang on Linux and MDVIEWER_ENABLE_SANITIZERS=ON")
    endif()
    foreach(target IN LISTS mdviewer_instrumented_targets)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
    endforeach()
    foreach(kind markdown svg)
        add_executable(mdviewer_${kind}_fuzz tools/fuzz/${kind}_fuzz.cpp)
        target_link_libraries(mdviewer_${kind}_fuzz PRIVATE mdviewer_core)
        target_compile_options(mdviewer_${kind}_fuzz PRIVATE -fsanitize=fuzzer)
        target_link_options(mdviewer_${kind}_fuzz PRIVATE -fsanitize=fuzzer)
        list(APPEND mdviewer_executables mdviewer_${kind}_fuzz)
        # Mutated corpus entries belong in the build tree, never in the checkout.
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus/${kind}")
        configure_file("tools/fuzz/corpus/${kind}/seed.txt"
            "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus/${kind}/seed.txt" COPYONLY)
        add_test(NAME mdviewer_${kind}_fuzz_smoke COMMAND mdviewer_${kind}_fuzz
            -runs=500 -max_len=16384 -timeout=5
            "${CMAKE_CURRENT_BINARY_DIR}/fuzz-corpus/${kind}")
        set_tests_properties(mdviewer_${kind}_fuzz_smoke PROPERTIES
            LABELS fuzz TIMEOUT 60 WORKING_DIRECTORY "${MDVIEWER_RUNTIME_DIR}")
    endforeach()
endif()

foreach(target IN LISTS mdviewer_executables)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MDVIEWER_RUNTIME_DIR}")
    add_dependencies(${target} mdviewer_runtime_resources)
    if(MDVIEWER_ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE
            -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=vptr)
    endif()
endforeach()
if(MSVC)
    foreach(target mdviewer_core ${mdviewer_executables})
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /wd4244 /wd4267 /wd5030)
    endforeach()
    set_source_files_properties(
        ${TREE_SITTER_CORE_SOURCE} ${TREE_SITTER_LANGUAGE_SOURCES} ${TREE_SITTER_QUERY_SOURCE}
        PROPERTIES COMPILE_OPTIONS /w)
    foreach(target mdviewer_core ${mdviewer_executables} md4c utf8proc tinyxml2 microtex)
        set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()

add_test(NAME mdviewer_unit_tests COMMAND mdviewer_tests)
set_tests_properties(mdviewer_unit_tests PROPERTIES
    TIMEOUT 180 WORKING_DIRECTORY "${MDVIEWER_RUNTIME_DIR}")
