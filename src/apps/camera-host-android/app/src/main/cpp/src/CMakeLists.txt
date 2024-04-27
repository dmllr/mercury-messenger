set(shared_libs "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../lib")

add_library(camera-lib SHARED
  lib.cpp
)

target_compile_features(camera-lib PUBLIC cxx_std_20)

target_link_libraries(camera-lib
  android
  log
  hela-eillance-core
)

target_include_directories(camera-lib
  PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${CMAKE_BINARY_DIR}"
  "${shared_libs}"
)

# workaround for "ninja: error: build.ninja multiple rules generate tests"
# remove it, build, undo removal, build again
add_test(tests
  tests.cpp
)

add_library(lib-tests SHARED
    tests.cpp
)
target_link_libraries(lib-tests
    PRIVATE
    $<TARGET_OBJECTS:adder>
    googletest::gtest
    junit-gtest::junit-gtest
)