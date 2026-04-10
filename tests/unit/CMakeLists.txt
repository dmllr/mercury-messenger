set(CMAKE_USE_WIN32_THREADS_INIT ON)
set(gtest_disable_pthreads ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(h-e-tests
  test-account-service.cpp
  test-args-parser.cpp
  test-attachments.cpp
  test-audio-encoder.cpp
  test-backup-manager.cpp
  test-bit-reader.cpp
  test-bot-api.cpp
  test-call-view-model.cpp
  test-circular-buffer.cpp
  test-client-core.cpp
  test-contact-id.cpp
  test-contact-manager.cpp
  test-contact-service.cpp
  test-contacts-view-model.cpp
  test-conversation-view-model.cpp
  test-crypto.cpp
  test-debug-view-model.cpp
  test-dispatch-queue.cpp
  test-event-queue.cpp
  test-file-transfer-cancellation.cpp
  test-file-transfer-resilience.cpp
  test-h264-utils.cpp
  test-id-manager.cpp
  test-internal-messaging-service.cpp
  test-ledger-projection.cpp
  test-media-call-service.cpp
  test-message-history.cpp
  test-messaging-service.cpp
  test-packet.cpp
  test-profile-service.cpp
  test-profile-updates.cpp
  test-profile-view-model.cpp
  test-protocol-safety.cpp
  test-resilience.cpp
  test-rtc-transport.cpp
  test-replies.cpp
  test-settings-manager.cpp
  test-settings-view-model.cpp
  test-signaling-negotiations.cpp
  test-sqlite-storage.cpp
  test-stream.cpp
  test-stream-source.cpp
  test-sync-engine.cpp
  test-time.cpp
  test-transport-peer.cpp
  test-utils-memory.cpp
  test-video-config.cpp
)
target_link_libraries(h-e-tests
  GTest::gtest_main
  eillance-core
  eillance-crypto
  eillance-capture
  eillance-rtc
  eillance-h264
  eillance-bot-api
)

include(GoogleTest)
gtest_discover_tests(h-e-tests)
