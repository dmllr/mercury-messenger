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
set (shared_cmake_libs_path ${root}/src/cmake)
include("${shared_cmake_libs_path}/enums/enums.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libdatachannel.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")
if (NOT TARGET nlohmann_json)
  include("${shared_cmake_libs_path}/libjson.cmake")
endif()
include("cmake/libliveMedia.cmake")

add_subdirectory("src")
