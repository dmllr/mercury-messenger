cmake_minimum_required(VERSION 3.7)

project(eillance)

include("src/cmake/setup.cmake")

find_package(Threads REQUIRED)

include("src/cmake/import_remote.cmake")

add_subdirectory("src")
add_subdirectory("tests")
