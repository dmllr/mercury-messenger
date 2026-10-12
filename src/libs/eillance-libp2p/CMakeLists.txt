cmake_minimum_required(VERSION 3.10)
project(eillance-libp2p)

set(CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")

if(ANDROID)
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
        set(RUST_TARGET "aarch64-linux-android")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
        set(RUST_TARGET "armv7-linux-androideabi")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
        set(RUST_TARGET "x86_64-linux-android")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
        set(RUST_TARGET "i686-linux-android")
    endif()
    
    set(LIBP2P_STATIC_LIB "${CARGO_TARGET_DIR}/${RUST_TARGET}/release/libeillance_libp2p.a")
    
    # Forward the NDK compiler to Cargo as the linker
    string(TOUPPER "${RUST_TARGET}" RUST_TARGET_UPPER)
    string(REPLACE "-" "_" RUST_TARGET_UPPER "${RUST_TARGET_UPPER}")
    string(REPLACE "-" "_" RUST_TARGET_ENV "${RUST_TARGET}")
    
    # Android NDK's clang needs the --target flag to invoke the correct linker backend (ELF lld vs PE lld-link)
    set(RUST_RUSTFLAGS "-Clinker=${CMAKE_C_COMPILER} -Clink-arg=--target=${CMAKE_C_COMPILER_TARGET}")
    
    add_custom_command(
        OUTPUT "${LIBP2P_STATIC_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env 
            "CARGO_TARGET_${RUST_TARGET_UPPER}_LINKER=${CMAKE_C_COMPILER}"
            "CARGO_TARGET_${RUST_TARGET_UPPER}_RUSTFLAGS=${RUST_RUSTFLAGS}"
            "CC_${RUST_TARGET_ENV}=${CMAKE_C_COMPILER}" 
            "CXX_${RUST_TARGET_ENV}=${CMAKE_CXX_COMPILER}" 
            "AR_${RUST_TARGET_ENV}=${CMAKE_AR}" 
            "CFLAGS_${RUST_TARGET_ENV}=--target=${CMAKE_C_COMPILER_TARGET}"
            "CXXFLAGS_${RUST_TARGET_ENV}=--target=${CMAKE_C_COMPILER_TARGET}"
            cargo build --release --target ${RUST_TARGET} --target-dir "${CARGO_TARGET_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs" "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
        COMMENT "Building Rust libp2p crate for Android (${RUST_TARGET})"
    )
else()
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
        COMMENT "Building Rust libp2p crate for host"
    )
endif()

add_custom_target(eillance_libp2p_cargo DEPENDS "${LIBP2P_STATIC_LIB}")

add_library(eillance-libp2p INTERFACE)
add_dependencies(eillance-libp2p eillance_libp2p_cargo)
target_include_directories(eillance-libp2p INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(eillance-libp2p INTERFACE "${LIBP2P_STATIC_LIB}")

if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(eillance-libp2p INTERFACE pthread dl m)
elseif(WIN32)
    target_link_libraries(eillance-libp2p INTERFACE ws2_32 userenv bcrypt ntdll)
elseif(ANDROID)
    target_link_libraries(eillance-libp2p INTERFACE dl m)
endif()
