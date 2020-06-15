cmake_minimum_required(VERSION 3.4.1)
project(hela-surveillance)

find_library(log-lib log)

include(cmake/import_remote.cmake)
include("cmake/live555.cmake")

add_subdirectory("src")
