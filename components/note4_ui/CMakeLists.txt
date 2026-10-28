idf_component_register(INCLUDE_DIRS "include")

# The compatibility header is consumed by first-party C callers in main.
# Keep the Rust-owned implementation behind the existing component archive.
target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
find_program(NOTE4_CARGO cargo REQUIRED)
set(rust_target "xtensa-esp32s3-none-elf")
set(cargo_target "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(archive "${cargo_target}/${rust_target}/release/libnote4_ui.a")
file(GLOB_RECURSE rust_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/src/*.rs")
file(GLOB assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../.note4-build/native-assets/*")
add_custom_command(OUTPUT "${archive}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${cargo_target}"
        "RUSTUP_TOOLCHAIN=esp" "RUSTFLAGS=" "CARGO_ENCODED_RUSTFLAGS=" "RUSTC_WRAPPER="
        "${NOTE4_CARGO}" build -Zbuild-std=core --release --locked --features firmware
        --target "${rust_target}" --manifest-path "${CMAKE_CURRENT_LIST_DIR}/Cargo.toml"
    DEPENDS ${rust_sources} ${assets} "${CMAKE_CURRENT_LIST_DIR}/Cargo.toml" "${CMAKE_CURRENT_LIST_DIR}/Cargo.lock"
    VERBATIM)
add_custom_target(note4_ui_build DEPENDS "${archive}")
add_library(note4_ui_native STATIC IMPORTED GLOBAL)
set_target_properties(note4_ui_native PROPERTIES IMPORTED_LOCATION "${archive}")
add_dependencies(note4_ui_native note4_ui_build)
target_link_libraries(${COMPONENT_LIB} INTERFACE note4_ui_native)
