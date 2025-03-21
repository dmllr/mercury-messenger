cmake_minimum_required(VERSION 3.7)

project(eillance)

set (root "../../../../../../..")
set (shared_src_path "${root}/src")
set (shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/setup.cmake")

find_library(log-lib log)

include("${shared_cmake_libs_path}/import_remote.cmake")

include("${shared_cmake_libs_path}/libboringssl.cmake")
add_subdirectory("${shared_src_path}/core" "${CMAKE_CURRENT_BINARY_DIR}/hela-eillance-core")
add_subdirectory("src")
