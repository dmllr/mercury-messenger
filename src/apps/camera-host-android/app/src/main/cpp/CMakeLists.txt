cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

enable_language(C)
enable_language(CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_library(log-lib log)

set (root ../../../../../../..)
set (shared_src_path ${root}/src)
set (shared_cmake_libs_path ${shared_src_path}/cmake)
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libdatachannel.cmake")
include("${shared_cmake_libs_path}/libopencv.cmake")
include("${shared_cmake_libs_path}/libffmpeg.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")
if (NOT TARGET nlohmann_json)
  include("${shared_cmake_libs_path}/libjson.cmake")
endif()
include("cmake/libliveMedia.cmake")

add_subdirectory("${shared_src_path}/core" "${CMAKE_CURRENT_BINARY_DIR}/hela-eillance-core")
add_subdirectory("src")
