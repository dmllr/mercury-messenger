cmake_minimum_required(VERSION 3.7)

project(eillance)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set (shared_cmake_libs_path "${REPO_ROOT}/src/cmake")
include("${shared_cmake_libs_path}/setup.cmake")

find_library(log-lib log)

include("${shared_cmake_libs_path}/import_remote.cmake")

add_subdirectory("${REPO_ROOT}/src/libs/eillance-logging" "${CMAKE_CURRENT_BINARY_DIR}/eillance-logging")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-utils" "${CMAKE_CURRENT_BINARY_DIR}/eillance-utils")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-crypto" "${CMAKE_CURRENT_BINARY_DIR}/eillance-crypto")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-storage-sqlite" "${CMAKE_CURRENT_BINARY_DIR}/eillance-storage-sqlite")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-libp2p" "${CMAKE_CURRENT_BINARY_DIR}/eillance-libp2p")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-core" "${CMAKE_CURRENT_BINARY_DIR}/eillance-core")
add_subdirectory("src")
