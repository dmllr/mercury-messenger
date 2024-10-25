set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(camera-lib SHARED
  lib.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_20)

target_link_libraries(camera-lib
  android
  camera2ndk
  mediandk
  log
  hela-eillance-core
)

# workaround for "ninja: error: build.ninja multiple rules generate tests"
# remove it, build, undo removal, build again
add_test(tests
  tests.cpp
)
