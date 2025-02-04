set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(lib-performer-service-bridge SHARED
    performer_service_bridge.cpp
)
target_compile_features(lib-performer-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-performer-service-bridge
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

add_library(lib-observer-service-bridge SHARED
    observer_service_bridge.cpp
)
target_compile_features(lib-observer-service-bridge PUBLIC cxx_std_20)
target_link_libraries(lib-observer-service-bridge
  android
  mediandk
  log
  hela-eillance-core
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(lib-performer-service-bridge PUBLIC CHECK_JNI)
  target_compile_definitions(lib-observer-service-bridge PUBLIC CHECK_JNI)
endif()
