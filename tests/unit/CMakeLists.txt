add_subdirectory("${REPO_ROOT}/src/libs/eillance-capture" "${CMAKE_CURRENT_BINARY_DIR}/eillance-capture")
add_subdirectory("${REPO_ROOT}/src/libs/eillance-rtc" "${CMAKE_CURRENT_BINARY_DIR}/eillance-rtc")

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
  test-call-id.cpp
  test-call-view-model.cpp
  test-capture.cpp
  test-channel-manager.cpp
  test-circular-buffer.cpp
  test-client-core.cpp
  test-contact-id.cpp
  test-contact-manager.cpp
  test-contact-service.cpp
  test-contacts-view-model.cpp
  test-conversation-view-model.cpp
  test-crypto.cpp
  test-data-transport.cpp
  test-delivery-service.cpp
  test-dispatch-queue.cpp
  test-event-queue.cpp
  test-file-transfer-cancellation.cpp
  test-file-transfer-resilience.cpp
  test-group-manager.cpp
  test-h264-utils.cpp
  test-id-manager.cpp
  test-internal-messaging-service.cpp
  test-ledger-projection.cpp
  test-ledger-hash.cpp
  test-media-call-service.cpp
  test-media-transport.cpp
  test-message-history.cpp
  test-messaging-service.cpp
  test-network-config.cpp
  test-packet.cpp
  test-profile-service.cpp
  test-profile-updates.cpp
  test-profile-view-model.cpp
  test-protocol.cpp
  test-protocol-safety.cpp
  test-replies.cpp
  test-resilience.cpp
  test-settings-manager.cpp
  test-settings-view-model.cpp
  test-sqlite-storage.cpp
  test-sync-engine.cpp
  test-time.cpp
  test-topic-id.cpp
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
  eillance-bot-api
)

include(GoogleTest)
gtest_discover_tests(h-e-tests)
