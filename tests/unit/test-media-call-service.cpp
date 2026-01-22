#include <gtest/gtest.h>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

// Mock capture implementation for duck-typing
class MockCaptureImpl {
 public:
  void Configure(Config) {}
  void Engage() {}
  void Revoke() {}
  auto FetchBuffers() -> Packet { return Packet(); }
  auto GetSampleDuration() -> Time::Unit { return 10000; }
};

class MediaCallServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 1. Setup transport
    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_raw_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    // 2. Setup messaging dependencies
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));
    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    auto conn_handler = [](ContactID const&) {};

    // 3. Initialize messaging service (needed by MediaCallService)
    messaging_service_ =
        std::make_unique<services::MessagingService>(storage_, transport_, contact_manager, conn_handler);

    // 4. Initialize MediaCallService
    service_ = std::make_unique<services::MediaCallService>(transport_);
    service_->SetMessagingService(messaging_service_.get());

    // 5. Initialize Capture (needed for av_stream_ invariant)
    service_->SetVideoCapture(std::make_unique<MockCaptureImpl>());
    service_->SetAudioCapture(std::make_unique<MockCaptureImpl>());
    service_->Configure({});

    // 6. Set dummy callbacks
    service_->SetIncomingCallCallback([](auto const&, auto) {});
    service_->SetCallAcceptedCallback([](auto const&) {});
    service_->SetCallTerminatedCallback([](auto const&) {});
    service_->SetCallRingingCallback([](auto const&) {});
  }

  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_raw_{};
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<services::MessagingService> messaging_service_;
  std::unique_ptr<services::MediaCallService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(MediaCallServiceTest, StartVoiceCall) {
  // Ensure contact exists for messaging service to not complain (optional but good practice)

  // Note: StartVoiceCallInternal sends a system message via messaging service.
  // MessagingService::SendSystemMessage calls AddTextEntry.

  service_->StartVoiceCall(peer1);

  // Verify that a system message was added to history
  auto history = messaging_service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  auto const& entry = history.messages[0];
  EXPECT_EQ(entry.kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_EQ(entry.direction, messaging::MessageDirection::sent);

  // Content check (approximate)
  auto const& data = std::get<messaging::MessageEntry>(entry.data);
  EXPECT_NE(data.content.find("call_request"), std::string::npos);
}

TEST_F(MediaCallServiceTest, AcceptCall) {
  // We need a peer to exist in transport
  auto peer = std::make_shared<transport::Peer>(peer1);
  transport_->AddPeer(peer1, peer);

  bool accepted_cb = false;
  service_->SetCallAcceptedCallback([&](auto const& id) {
    if (id == peer1) accepted_cb = true;
  });

  service_->AcceptCall(peer1, messaging::HistoryEntryKind::voiceCall);

  EXPECT_TRUE(accepted_cb);
  EXPECT_TRUE(peer->has_media_peer_connection);

  // Verify system message sent (call_accept)
  auto history = messaging_service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  auto const& data = std::get<messaging::MessageEntry>(history.messages[0].data);
  EXPECT_NE(data.content.find("call_accept"), std::string::npos);
}

TEST_F(MediaCallServiceTest, Hangup) {
  auto peer = std::make_shared<transport::Peer>(peer1);
  // Simulate active media connection
  peer->has_media_peer_connection = true;
  peer->data_channel_is_open = true;  // allow sending hangup message
  transport_->AddPeer(peer1, peer);

  bool terminated_cb = false;
  service_->SetCallTerminatedCallback([&](auto const& id) {
    if (id == peer1) terminated_cb = true;
  });

  service_->Hangup(peer1, true);  // notify_peer = true

  EXPECT_TRUE(terminated_cb);
  EXPECT_FALSE(peer->has_media_peer_connection);

  // Verify transport closed media connection
  bool media_closed = false;
  for (auto const& [pid, ctx] : mock_transport_raw_->log.closed_connections) {
    if (pid == peer1 && ctx == "media") media_closed = true;
  }
  EXPECT_TRUE(media_closed);

  // Verify hangup message sent
  bool hangup_sent = false;
  for (auto const& [pid, msg] : mock_transport_raw_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "hangup") hangup_sent = true;
  }
  EXPECT_TRUE(hangup_sent);
}
