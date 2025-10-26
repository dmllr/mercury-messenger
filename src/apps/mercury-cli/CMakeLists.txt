project(mercury-cli CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

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
  eillance-rtc
  libftxui
  libopencv
  libffmpeg
  libminiaudio
  opus
)
