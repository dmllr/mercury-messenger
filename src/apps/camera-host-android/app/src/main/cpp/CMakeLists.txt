cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

enable_language(C)
enable_language(CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0077 NEW)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

find_library(log-lib log)

set (root ../../../../../../..)
set (shared_src_path ${root}/src)
set (shared_cmake_libs_path ${shared_src_path}/cmake)
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libdatachannel.cmake")
include("${shared_cmake_libs_path}/libopencv.cmake")
include("${shared_cmake_libs_path}/libffmpeg.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")

add_subdirectory("${shared_src_path}/core" "${CMAKE_CURRENT_BINARY_DIR}/hela-eillance-core")
add_subdirectory("src")
