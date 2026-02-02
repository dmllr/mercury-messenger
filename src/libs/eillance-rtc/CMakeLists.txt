project(eillance-rtc CXX) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_cmake_libs_path "${REPO_ROOT}/src/cmake")

include("${shared_cmake_libs_path}/libdatachannel.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")

add_library(eillance-rtc INTERFACE)
target_compile_features(eillance-rtc INTERFACE cxx_std_20)
target_include_directories(eillance-rtc INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(eillance-rtc INTERFACE
  eillance-core
  datachannel-static
  plog
  nlohmann_json
  srtp2
)

if (WIN32)
  target_link_libraries(eillance-rtc INTERFACE Ws2_32 Secur32 Bcrypt strmiids mfplat mfuuid)
endif ()
