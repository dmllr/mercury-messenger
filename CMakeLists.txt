cmake_minimum_required(VERSION 4.0)

project(eillance)

include("src/cmake/setup.cmake")
include("src/cmake/import_remote.cmake")

option(EILLANCE_BUILD_TESTS "Enable tests" ON)

find_package(Threads REQUIRED)

add_subdirectory("src")
if (EILLANCE_BUILD_TESTS)
  add_subdirectory("tests")
endif()
