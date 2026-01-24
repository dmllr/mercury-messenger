#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

// Mock capture implementation for duck-typing
class MockCapture {
 public:
  void Configure(Config) {}
  void Engage() {}
  void Revoke() {}
  auto FetchBuffers() -> Packet { return Packet(); }
  auto GetSampleDuration() -> Time::Unit { return 10000; }
};

class ResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 1. Storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // 2. Transport
    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    // 3. Client
    client_ = std::make_shared<Client>(storage_, transport_);

    // 4. Inject Mock Capture (to satisfy invariants)
    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    // 5. Setup dummy callbacks to satisfy invariants during operations
    client_->GetAccountService()->SetOnMyProfileUpdatedCallback([]() {});
    client_->GetContactService()->SetContactRequestCallback([](auto const&) {});
    client_->GetContactService()->SetContactStatusChangedCallback([](auto const&, auto) {});
    client_->GetContactService()->SetContactProfileUpdatedCallback([](auto const&) {});
    client_->GetContactService()->SetContactRemovedCallback([](auto const&) {});
    client_->GetContactService()->SetPeerListUpdateCallback([](auto const&) {});
    client_->GetContactService()->SetProfileReceivedCallback([](auto const&, auto const&) {});
    client_->GetMediaCallService()->SetIncomingCallCallback([](auto const&, auto) {});
    client_->GetMediaCallService()->SetCallRingingCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallAcceptedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallTerminatedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetFrameReceivedCallback([](auto const&) {});
    client_->GetMessagingService()->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    client_->GetMessagingService()->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});
    client_->SetStatusCallback([](auto const&, auto, auto) {});

    // 6. Setup Identity
    (void)client_->GetAccountService()->GenerateNewIdentity();

    // 7. Setup a Confirmed Contact
    // AddContact triggers connection logic, so peer1 becomes connected here.
    client_->AddContact(peer1, std::nullopt);
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->GetContactService()->DebugReloadCache();

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
  }

  void FlushClientQueue() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

// Test that pending read receipts persist and are sent on connection
TEST_F(ResilienceTest, PendingReadReceiptsPersistAndSend) {
  // 1. Simulate receiving a message with remote ID
  auto entry = mock_storage_->AddHistoryEntry({
      .remote_id = 999,
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::received,
      .status = messaging::MessageStatus::delivered,
      .data = messaging::MessageEntry{"hello"},
      .peer_id = peer1
  });

  // 2. Mark as read while disconnected
  // Explicitly remove peer to simulate disconnection, as SetUp connects it.
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->MarkMessagesAsRead(peer1, {entry.id});
  FlushClientQueue();

  // 3. Verify it is pending in storage
  auto pending = mock_storage_->GetPendingReadReceipts(peer1);
  ASSERT_EQ(pending.size(), 1);
  EXPECT_EQ(pending[0], 999);

  // 4. Connect
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Simulate Data Channel Open (triggers queue flush)
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // 5. Verify receipt sent
  bool receipt_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "message_read") {
      auto ids = msg["message_ids"].get<std::vector<uint64_t>>();
      if (ids.size() == 1 && ids[0] == 999) {
        receipt_sent = true;
      }
    }
  }
  EXPECT_TRUE(receipt_sent);

  // 6. Verify removed from storage (assuming optimistic removal)
  pending = mock_storage_->GetPendingReadReceipts(peer1);
  EXPECT_TRUE(pending.empty());
}

// Test that profile updates persist as a "dirty" flag and sync on connection
TEST_F(ResilienceTest, ProfileUpdateSyncsOnConnect) {
  // 1. Update Profile while disconnected
  // Explicitly remove peer to simulate disconnection
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->UpdateMyProfile("New Name", "Bio", "");
  FlushClientQueue();

  // 2. Verify contact marked dirty in storage
  auto contact = mock_storage_->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_TRUE(contact->profile_sync_needed);

  // 3. Connect
  client_->Connect(peer1);
  FlushClientQueue();

  // 4. Simulate Data Channel Open
  mock_transport_->log.sent_peer_messages.clear();
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // 5. Verify profile sent
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "New Name");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent);

  // 6. Verify flag cleared
  contact = mock_storage_->GetContact(peer1);
  EXPECT_FALSE(contact->profile_sync_needed);
}
