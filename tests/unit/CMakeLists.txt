set(CMAKE_USE_WIN32_THREADS_INIT ON)
set(gtest_disable_pthreads ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(h-e-tests
  test-audio-encoder.cpp
  test-dispatch-queue.cpp
  test-event-queue.cpp
  test-string-utils.cpp
  test-h264-utils.cpp
)
target_link_libraries(h-e-tests GTest::gtest_main hela-eillance-core)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../files" DESTINATION "${CMAKE_BINARY_DIR}/bin")

include(GoogleTest)
gtest_discover_tests(h-e-tests)
