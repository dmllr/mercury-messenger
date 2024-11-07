set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(app-engine-lib SHARED
  lib.cpp
)

target_compile_features(app-engine-lib PUBLIC cxx_std_20)

target_link_libraries(app-engine-lib
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(app-engine-lib PUBLIC CHECK_JNI)
endif()


enable_testing()

set(GTEST_ROOT "${ANDROID_NDK}/sources/third_party/googletest")
add_library(gtest STATIC "${GTEST_ROOT}/src/gtest_main.cc" "${GTEST_ROOT}/src/gtest-all.cc")
target_include_directories(gtest PRIVATE "${GTEST_ROOT}")
target_include_directories(gtest PUBLIC "${GTEST_ROOT}/include")

add_executable(app-engine-tests
  tests.cpp
)

target_link_libraries(app-engine-tests
    gtest
    app-engine-lib
)
