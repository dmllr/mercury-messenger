set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(lib-streamer-service-bridge SHARED
    streamer_service_bridge.cpp
)
target_compile_features(lib-streamer-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-streamer-service-bridge
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

add_library(lib-viewer-service-bridge SHARED
    viewer_service_bridge.cpp
)
target_compile_features(lib-viewer-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-viewer-service-bridge
  android
  mediandk
  log
  hela-eillance-core
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(lib-streamer-service-bridge PUBLIC CHECK_JNI)
  target_compile_definitions(lib-viewer-service-bridge PUBLIC CHECK_JNI)
endif()
