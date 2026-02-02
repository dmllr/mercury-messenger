execute_process(
  COMMAND git rev-parse --show-toplevel
  OUTPUT_VARIABLE REPO_ROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(shared_libs "${REPO_ROOT}/lib")

add_library(lib-client-service-bridge SHARED
  client_service_bridge.cpp
)
target_compile_features(lib-client-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-client-service-bridge
  eillance-utils
  eillance-logging
  eillance-core
  android
  camera2ndk
  mediandk
  log
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(lib-client-service-bridge PUBLIC CHECK_JNI)
endif()
