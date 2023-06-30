cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

find_library(log-lib log)

set (root ../../../../../../../..)
set (shared_cmake_libs_path ${root}/src/cmake)
include(${shared_cmake_libs_path}/import_remote.cmake)
include("${shared_cmake_libs_path}/libopenssl.cmake")
include("${shared_cmake_libs_path}/libdatachannel.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")

include("cmake/libliveMedia.cmake")

add_subdirectory("src")
