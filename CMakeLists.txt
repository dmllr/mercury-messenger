cmake_minimum_required(VERSION 3.7)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY")

project(hela-surveillance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

include("src/cmake/import_remote.cmake")

include("src/cmake/libdatachannel.cmake")
include("src/cmake/libopencv.cmake")
include("src/cmake/libffmpeg.cmake")
include("src/cmake/libminiaudio.cmake")
include("src/cmake/libjson.cmake")

add_subdirectory("src/core")
add_subdirectory(src/apps/camera-host-cli-cp)
add_subdirectory(src/tests)
