project(mercury-cli CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

add_subdirectory("${REPO_ROOT}/src/libs/eillance-capture" "${CMAKE_CURRENT_BINARY_DIR}/eillance-capture")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-rtc" "${CMAKE_CURRENT_BINARY_DIR}/eillance-rtc")

include("${shared_cmake_libs_path}/setup.cmake")
include("${shared_cmake_libs_path}/import_remote.cmake")
include("${shared_cmake_libs_path}/libftxui.cmake")
include("${shared_cmake_libs_path}/libopencv.cmake")
include("${shared_cmake_libs_path}/libffmpeg.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")

add_executable(mercury-cli
  mercury.cpp
)

set_property(TARGET mercury-cli PROPERTY CXX_STANDARD 20)
target_compile_features(mercury-cli PRIVATE cxx_std_20)

target_link_libraries(mercury-cli PRIVATE
  eillance-core
  eillance-capture
  eillance-rtc
  eillance-utils
  libftxui
  libopencv
  libffmpeg
  libminiaudio
  opus
)
