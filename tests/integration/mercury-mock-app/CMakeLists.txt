project(mercury-mock-app CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/setup.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")

add_executable(mercury-mock-app main.cpp)

set_property(TARGET mercury-mock-app PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-mock-app PRIVATE cxx_std_20)

target_link_libraries(mercury-mock-app PRIVATE
  eillance-core
  eillance-rtc
  eillance-utils
  eillance-logging
)
