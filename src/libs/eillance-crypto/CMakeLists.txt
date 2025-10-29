add_library(eillance-crypto INTERFACE)

set(root "../../..")
set(shared_src_path "${root}/src")
set(shared_cmake_libs_path "${shared_src_path}/cmake")

include("${shared_cmake_libs_path}/libboringssl.cmake")

target_include_directories(eillance-crypto INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(eillance-crypto INTERFACE
  eillance-logging
  ssl
  crypto
)

if (WIN32)
  target_link_libraries(eillance-crypto INTERFACE Ws2_32 Secur32 Bcrypt)
endif ()
if(ANDROID)
  target_link_libraries(eillance-crypto INTERFACE log)
endif()
