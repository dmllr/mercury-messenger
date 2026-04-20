cmake_minimum_required(VERSION 3.10)
project(eillance-libp2p)

set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")

if (WIN32)
    set(LIBP2P_STATIC_LIB "${CARGO_TARGET_DIR}/release/eillance_libp2p.lib")
else()
    set(LIBP2P_STATIC_LIB "${CARGO_TARGET_DIR}/release/libeillance_libp2p.a")
endif()

add_custom_command(
    OUTPUT "${LIBP2P_STATIC_LIB}"
    COMMAND cargo build --release --target-dir "${CARGO_TARGET_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs" "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    COMMENT "Building Rust libp2p crate for eillance-core"
)

add_custom_target(eillance_libp2p_cargo DEPENDS "${LIBP2P_STATIC_LIB}")

add_library(eillance-libp2p STATIC IMPORTED)
add_dependencies(eillance-libp2p eillance_libp2p_cargo)
set_target_properties(eillance-libp2p PROPERTIES IMPORTED_LOCATION "${LIBP2P_STATIC_LIB}")
target_include_directories(eillance-libp2p INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
