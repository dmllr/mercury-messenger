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
  test-account-service.cpp
  test-backup-manager.cpp
  test-audio-encoder.cpp
  test-bit-reader.cpp
  test-call-view-model.cpp
  test-client-core.cpp
  test-contact-service.cpp
  test-contacts-view-model.cpp
  test-conversation-view-model.cpp
  test-delivery-service.cpp
  test-dispatch-queue.cpp
  test-event-queue.cpp
  test-h264-utils.cpp
  test-args-parser.cpp
  test-circular-buffer.cpp
  test-contact-manager.cpp
  test-crypto.cpp
  test-id-manager.cpp
  test-media-call-service.cpp
  test-message-history.cpp
  test-packet.cpp
  test-profile-view-model.cpp
  test-settings-manager.cpp
  test-settings-view-model.cpp
  test-sqlite-storage.cpp
  test-stream.cpp
)
target_link_libraries(h-e-tests
  GTest::gtest_main
  eillance-core
  eillance-crypto
  eillance-capture
  eillance-rtc
  eillance-h264
)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../files" DESTINATION "${CMAKE_BINARY_DIR}/bin")

include(GoogleTest)
gtest_discover_tests(h-e-tests)
