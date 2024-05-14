include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(test-audio-encoder test-audio-encoder.cpp)
target_link_libraries(test-audio-encoder GTest::gtest_main hela-eillance-core)

include(GoogleTest)
gtest_discover_tests(test-audio-encoder)
