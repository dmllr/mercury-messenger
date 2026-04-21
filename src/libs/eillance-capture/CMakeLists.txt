if (TARGET eillance-capture)
  return()
endif ()

set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libopus.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")

add_library(eillance-capture INTERFACE)
target_compile_features(eillance-capture INTERFACE cxx_std_20)
target_include_directories(eillance-capture INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(eillance-capture INTERFACE
  libminiaudio
  opus
  eillance-core
)
