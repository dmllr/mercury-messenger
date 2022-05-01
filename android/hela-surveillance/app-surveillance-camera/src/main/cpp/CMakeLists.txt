cmake_minimum_required(VERSION 3.7)
project(hela-surveillance)

find_library(log-lib log)

include(cmake/import_remote.cmake)
include("cmake/libopenssl.cmake")
include("cmake/libliveMedia.cmake")
include("cmake/libdatachannel.cmake")
include("cmake/libopus.cmake")

add_subdirectory("src")
