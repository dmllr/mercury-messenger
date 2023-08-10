cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("src/cmake/import_remote.cmake")
include("src/cmake/libssl.cmake")
include("src/cmake/libdatachannel.cmake")
include("src/cmake/libopencv.cmake")
include("src/cmake/libffmpeg.cmake")

add_subdirectory(src/apps/posix-server)
add_subdirectory(src/apps/posix-receiver)
