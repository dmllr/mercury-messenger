set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(lib-client-service-bridge SHARED
  client_service_bridge.cpp
)
target_compile_features(lib-client-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-client-service-bridge
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(lib-client-service-bridge PUBLIC CHECK_JNI)
endif()
