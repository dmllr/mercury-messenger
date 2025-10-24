add_library(eillance-core INTERFACE)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/libboringssl.cmake")
include("${shared_cmake_libs_path}/libplog.cmake")
include("${shared_cmake_libs_path}/libjson.cmake")
include("${shared_cmake_libs_path}/libminiaudio.cmake")
include("${shared_cmake_libs_path}/libopus.cmake")


target_include_directories(eillance-core INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-core INTERFACE
  eillance-utils
  plog
  nlohmann_json
  ssl
  crypto
  libminiaudio
  opus
)

if (WIN32)
  target_link_libraries(eillance-core INTERFACE Ws2_32 Secur32 Bcrypt)
endif ()
if(ANDROID)
  target_link_libraries(eillance-core INTERFACE log)
endif()
