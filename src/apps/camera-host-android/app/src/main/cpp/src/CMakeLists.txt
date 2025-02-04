set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(lib-performer-service-impl SHARED
    performer_service_impl.cpp
)

target_compile_features(lib-performer-service-impl PUBLIC cxx_std_20)

target_link_libraries(lib-performer-service-impl
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(lib-performer-service-impl PUBLIC CHECK_JNI)
endif()
