# Native Mermaid -> SVG only. Cargo.lock pins the complete dependency graph.
find_program(MDVIEWER_CARGO cargo REQUIRED)
find_program(MDVIEWER_RUSTC rustc REQUIRED)
execute_process(COMMAND "${MDVIEWER_RUSTC}" -vV
    OUTPUT_VARIABLE rust_version_info COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([^\r\n]+)" rust_host_match "${rust_version_info}")
set(MDVIEWER_RUST_TARGET "${CMAKE_MATCH_1}" CACHE STRING "Rust target triple for the Mermaid bridge")
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "The Mermaid bridge currently supports native builds only")
endif()

set(mermaid_manifest "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mermaid_bridge/Cargo.toml")
set(mermaid_target_dir "${CMAKE_CURRENT_BINARY_DIR}/mermaid-rust")
if(WIN32)
    set(mermaid_library "${mermaid_target_dir}/${MDVIEWER_RUST_TARGET}/release/mdviewer_mermaid.lib")
    # Match the application's /MT runtime instead of Rust's default /MD.
    set(mermaid_rust_flags -C target-feature=+crt-static)
else()
    set(mermaid_library "${mermaid_target_dir}/${MDVIEWER_RUST_TARGET}/release/libmdviewer_mermaid.a")
    set(mermaid_rust_flags)
endif()
add_custom_command(OUTPUT "${mermaid_library}"
    COMMAND "${MDVIEWER_CARGO}" rustc --locked --release --lib
        --manifest-path "${mermaid_manifest}"
        --target "${MDVIEWER_RUST_TARGET}"
        --target-dir "${mermaid_target_dir}" -- ${mermaid_rust_flags}
    COMMAND "${CMAKE_COMMAND}" -E touch_nocreate "${mermaid_library}"
    DEPENDS "${mermaid_manifest}"
        "${CMAKE_CURRENT_LIST_FILE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mermaid_bridge/Cargo.lock"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mermaid_bridge/src/lib.rs"
    COMMENT "Building native Mermaid SVG library (Rust)"
    VERBATIM)
add_custom_target(mdviewer_mermaid_build DEPENDS "${mermaid_library}")
add_library(mdviewer_mermaid STATIC IMPORTED GLOBAL)
set_target_properties(mdviewer_mermaid PROPERTIES IMPORTED_LOCATION "${mermaid_library}")
add_dependencies(mdviewer_mermaid mdviewer_mermaid_build)
if(WIN32)
    set_property(TARGET mdviewer_mermaid PROPERTY INTERFACE_LINK_LIBRARIES
        "ws2_32;userenv;bcrypt;ntdll;advapi32;kernel32")
    # Rust's prebuilt standard library uses the release CRT. Debug C++ uses /MTd.
    set_property(TARGET mdviewer_mermaid PROPERTY INTERFACE_LINK_OPTIONS
        "$<$<CONFIG:Debug>:/NODEFAULTLIB:LIBCMT>")
else()
    set_property(TARGET mdviewer_mermaid PROPERTY INTERFACE_LINK_LIBRARIES
        "${CMAKE_DL_LIBS};pthread;m")
endif()
