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

    // Contact Service
    client_->GetContactService()->SetContactRequestCallback([](auto const&) {});
    client_->GetContactService()->SetContactStatusChangedCallback([](auto const&, auto) {});
    client_->GetContactService()->SetContactRemovedCallback([](auto const&) {});

    // Profile Service
    client_->GetProfileService()->SetContactProfileUpdatedCallback([](auto const&) {});
    client_->GetProfileService()->SetPeerListUpdateCallback([](auto const&) {});
    client_->GetProfileService()->SetProfileReceivedCallback([](auto const&, auto const&) {});

    // Media
    client_->GetMediaCallService()->SetIncomingCallCallback([](auto const&, auto) {});
    client_->GetMediaCallService()->SetCallRingingCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallAcceptedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallTerminatedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetFrameReceivedCallback([](auto const&) {});

    // Messaging
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

  void TearDown() override { client_->Terminate(); }

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
  auto entry = mock_storage_->AddHistoryEntry(
      {.remote_id = 999,
       .kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::delivered,
       .data = messaging::MessageEntry{"hello"},
       .peer_id = peer1}
  );

  // 2. Mark as read while disconnected
  // Explicitly remove peer to simulate disconnection, as SetUp connects it.
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->MarkMessagesAsRead(peer1, {entry.id});
  FlushClientQueue();

  // 3. Verify it is pending in storage
  auto pending = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(pending.size(), 1);
  nlohmann::json const json = nlohmann::json::parse(pending[0].payload);
  EXPECT_EQ(json["type"], "message_read");
  EXPECT_EQ(json["message_ids"][0], 999);

  // 4. Connect
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Simulate Data Channel Open (triggers queue flush)
  mock_transport_->log.sent_peer_messages.clear();
  // Using delegate to simulate transport event which Client listens to via InternalMessagingService
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
  pending = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(pending.empty());
}

// Test that profile updates persist as a queued action and sync on connection
TEST_F(ResilienceTest, ProfileUpdateSyncsOnConnect) {
  // 1. Update Profile while disconnected
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->UpdateMyProfile("New Name", "Bio", "");
  FlushClientQueue();

  // 2. Verify action queued
  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_FALSE(actions.empty());
  bool found_profile = false;
  for (auto const& act : actions) {
    if (act.payload.find("profile_info") != std::string::npos) {
      found_profile = true;
    }
  }
  EXPECT_TRUE(found_profile);

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

  // 6. Verify action removed
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

// Test that other pending actions (like handshake messages) are queued and sent
TEST_F(ResilienceTest, PendingActionsPersistAndSend) {
  // 1. Disconnect
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  // 2. Trigger an action (e.g. Reset Display Name -> profile_request)
  client_->ResetContactDisplayNameOverride(peer1);
  FlushClientQueue();

  // 3. Verify queued in storage
  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_FALSE(actions.empty());
  bool found_req = false;
  for (auto const& act : actions) {
    if (act.payload.find("profile_request") != std::string::npos) {
      found_req = true;
    }
  }
  EXPECT_TRUE(found_req);

  // 4. Connect
  client_->Connect(peer1);
  FlushClientQueue();

  // 5. Trigger Data Channel Open
  mock_transport_->log.sent_peer_messages.clear();
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // 6. Verify sent
  bool req_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_request") {
      req_sent = true;
    }
  }
  EXPECT_TRUE(req_sent);

  // 7. Verify removed
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

// Test scenario: User B reinstalled (lost contact list) receiving a message from User A.
// Expectation: B auto-creates the contact as CONFIRMED so the message is visible.
TEST_F(ResilienceTest, ReinstallRecovery_MessageReceipt) {
  // 1. Simulate "wipe" by removing peer1 from contacts.
  client_->DeleteContact(peer1);
  FlushClientQueue();
  ASSERT_FALSE(client_->GetContactService()->GetContact(peer1));

  // 2. Connect (as if A connected to B)
  client_->Connect(peer1);
  FlushClientQueue();
  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // 3. Simulate receiving a chat message
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  std::string plain = "Recovery Message";
  std::string payload;
  auto const version = services::MessagingService::currentMessageVersion;
  payload.push_back(static_cast<char>((version >> 8) & 0xFF));
  payload.push_back(static_cast<char>(version & 0xFF));
  payload.append(plain);
  auto encrypted = other.Encrypt(payload);

  nlohmann::json const chat_msg = {
      {"type", "chat"},
      {"message_id", 100},
      {"is_system", false},
      {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
  };

  // CORRECT: Inject message directly into the data channel handler, mimicking WebRTC transport
  ASSERT_TRUE(peer->on_data_channel_message);
  peer->on_data_channel_message(chat_msg.dump());
  FlushClientQueue();

  // 4. Verify contact was auto-created and CONFIRMED
  auto contact = client_->GetContactService()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  // 5. Verify message stored
  auto [messages, total_messages] = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(messages.empty());
  auto const& data = std::get<messaging::MessageEntry>(messages[0].data);
  EXPECT_EQ(data.content, "Recovery Message");
}

// Test scenario: User B reinstalled and tries to add User A.
// Expectation: A (confirmed) resends signature and profile, B confirms.
TEST_F(ResilienceTest, ReinstallRecovery_ContactAdd) {
  // 1. Ensure A is confirmed (Setup does this).
  ASSERT_TRUE(client_->GetContactService()->IsConfirmed(peer1));

  // 2. Simulate receiving "contact_request" (B adding A)
  nlohmann::json const req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Reinstalled B"}}}
  };

  mock_transport_->log.sent_signaling_messages.clear();
  mock_transport_->log.sent_peer_messages.clear();

  mock_transport_->SimulatePeerMessage(peer1, req_msg);
  FlushClientQueue();

  // 3. Verify A sends "contact_confirm" AND "contact_signature"
  bool confirm_sent = false;
  for (auto const& msg : mock_transport_->log.sent_signaling_messages) {
    if (msg["type"] == "contact_confirm" && msg["to"] == peer1.ToString()) {
      confirm_sent = true;
    }
  }
  EXPECT_TRUE(confirm_sent) << "Should send confirmation via signaling";

  // Simulate data channel open to flush queued signature
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  bool signature_sent = false;
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1) {
      if (msg["type"] == "contact_signature") signature_sent = true;
      if (msg["type"] == "profile_info") profile_sent = true;
    }
  }
  EXPECT_TRUE(signature_sent) << "Should send signature via data channel";
  EXPECT_TRUE(profile_sent) << "Should send profile via data channel";
}
