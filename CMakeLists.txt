cmake_minimum_required(VERSION 3.7)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(hela-surveillance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0077 NEW)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()

include("src/cmake/import_remote.cmake")

include("src/cmake/libdatachannel.cmake")
include("src/cmake/libopencv.cmake")
include("src/cmake/libffmpeg.cmake")
include("src/cmake/libopus.cmake")
include("src/cmake/libminiaudio.cmake")
include("src/cmake/libjson.cmake")

add_subdirectory("src")
add_subdirectory("tests")
