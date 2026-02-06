#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

// Mock capture implementation for duck-typing
class MockCapture {
 public:
  void Configure(Config) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> Packet {
    return Packet();
  }
  auto GetSampleDuration() -> Time::Unit {
    return 10000;
  }
};

class ResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    client_ = std::make_shared<Client>(storage_, transport_);

    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    client_->GetAccountService()->SetOnMyProfileUpdatedCallback([]() {});

    client_->GetContactService()->SetContactRequestCallback([](auto const&) {});
    client_->GetContactService()->SetContactStatusChangedCallback([](auto const&, auto) {});
    client_->GetContactService()->SetContactRemovedCallback([](auto const&) {});

    client_->GetProfileService()->SetContactProfileUpdatedCallback([](auto const&) {});

    client_->GetMediaCallService()->SetIncomingCallCallback([](auto const&, auto) {});
    client_->GetMediaCallService()->SetCallRingingCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallAcceptedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallTerminatedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetFrameReceivedCallback([](auto const&) {});

    client_->GetMessagingService()->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    client_->GetMessagingService()->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});

    client_->SetStatusCallback([](auto const&, auto, auto) {});

    (void)client_->GetAccountService()->GenerateNewIdentity();

    // Establish a confirmed contact context
    std::ignore = client_->AddContact(peer1, std::nullopt);
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->GetContactService()->ReloadCache();

    // Clear side-effects from AddContact (e.g. the initial contact_request action)
    // to ensure tests start with an empty pending queue.
    auto pending = mock_storage_->GetPendingActions(peer1);
    for (auto const& action : pending) {
      mock_storage_->RemovePendingAction(action.id);
    }

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
  }

  void FlushClientQueue() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Helper to wait for a condition to be true (polling with timeout)
  template <typename Func>
  static void WaitFor(Func condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    auto const start = std::chrono::steady_clock::now();
    while (!condition()) {
      if (std::chrono::steady_clock::now() - start > timeout) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ResilienceTest, PendingReadReceiptsPersistAndSend) {
  auto entry = mock_storage_->AddHistoryEntry(
      {.remote_id = 999,
       .kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::delivered,
       .data = messaging::MessageEntry{"hello"},
       .peer_id = peer1}
  );

  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->MarkMessagesAsRead(peer1, {entry.id});
  FlushClientQueue();

  auto pending = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(pending.size(), 1);
  nlohmann::json const json = nlohmann::json::parse(pending[0].payload);
  EXPECT_EQ(json["type"], "message_read");
  EXPECT_EQ(json["message_ids"][0], 999);

  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  mock_transport_->log.sent_peer_messages.clear();
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

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

  pending = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(pending.empty());
}

TEST_F(ResilienceTest, ProfileUpdateSyncsOnConnect) {
  // Ensure peer is disconnected so update queues instead of sends immediately
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->UpdateMyProfile("New Name", "Bio", "");
  FlushClientQueue();

  auto actions = mock_storage_->GetPendingActions(peer1);

  // The test failed here previously. We must ensure that UpdateMyProfile actually queues an action.
  // ProfileService::MarkAllContactsProfileSyncNeeded iterates confirmed contacts and calls SendMyProfile.
  // SendMyProfile calls AddPendingAction.
  // If this fails, it means either the contact isn't seen as confirmed or the loop failed.
  // In SetUp, we marked peer1 as confirmed.

  ASSERT_FALSE(actions.empty()) << "Profile update action was not queued";

  bool found_profile = false;
  for (auto const& act : actions) {
    if (act.payload.find("profile_info") != std::string::npos) {
      found_profile = true;
    }
  }
  EXPECT_TRUE(found_profile);

  client_->Connect(peer1);
  FlushClientQueue();

  mock_transport_->log.sent_peer_messages.clear();
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_info") {
      EXPECT_EQ(msg["display_name"], "New Name");
      profile_sent = true;
    }
  }
  EXPECT_TRUE(profile_sent);

  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

TEST_F(ResilienceTest, ReinstallRecovery_MessageReceipt) {
  // Simulate "wipe" by removing peer1 from contacts.
  client_->DeleteContact(peer1);
  FlushClientQueue();
  ASSERT_FALSE(client_->GetContactService()->GetContact(peer1));

  // Connect (as if A connected to B)
  client_->Connect(peer1);
  FlushClientQueue();
  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Simulate receiving a chat message
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  std::string plain = "Recovery Message";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = plain;

  auto encrypted = other.Encrypt(payload_json.dump());

  nlohmann::json const chat_msg = {
      {"type", "chat"},
      {"message_id", 100},
      {"is_system", false},
      {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
  };

  ASSERT_TRUE(peer->on_data_channel_message);

  // Convert JSON to bytes for binary-capable handler
  std::string msg_str = chat_msg.dump();
  std::vector<std::byte> msg_bytes(msg_str.size());
  std::transform(msg_str.begin(), msg_str.end(), msg_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });

  peer->on_data_channel_message(msg_bytes);
  FlushClientQueue();

  // Verify contact was auto-created and CONFIRMED
  auto contact = client_->GetContactService()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  // Verify message stored
  auto [messages, total_messages] = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(messages.empty());
  auto const& data = std::get<messaging::MessageEntry>(messages[0].data);
  EXPECT_EQ(data.content, "Recovery Message");
}

TEST_F(ResilienceTest, ReinstallRecovery_ContactAdd) {
  ASSERT_TRUE(client_->GetContactService()->IsConfirmed(peer1));

  nlohmann::json const req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Reinstalled B"}}}
  };

  mock_transport_->log.sent_signaling_messages.clear();
  mock_transport_->log.sent_peer_messages.clear();

  mock_transport_->SimulatePeerMessage(peer1, req_msg);
  FlushClientQueue();

  bool confirm_sent = false;
  for (auto const& msg : mock_transport_->log.sent_signaling_messages) {
    if (msg["type"] == "contact_confirm" && msg["to"] == peer1.ToString()) {
      confirm_sent = true;
    }
  }
  EXPECT_TRUE(confirm_sent) << "Should send confirmation via signaling";

  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  bool signature_sent = false;
  bool profile_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1) {
      if (msg["type"] == "contact_signature") {
        signature_sent = true;
      }
      if (msg["type"] == "profile_info") {
        profile_sent = true;
      }
    }
  }
  EXPECT_TRUE(signature_sent) << "Should send signature via data channel";
  EXPECT_TRUE(profile_sent) << "Should send profile via data channel";
}

TEST_F(ResilienceTest, MessageRedeliveryDeduplication) {
  // Simulate sender sending message
  // We manually inject the incoming message to receiver logic
  std::string content = "Unique Content";
  nlohmann::json payload;
  payload[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload[services::MessagingService::keyContent] = content;

  // Setup crypto context
  auto const peer = mock_transport_->GetPeer(peer1);
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  // Ensure the peer is considered "open" so receipt can be sent
  peer->data_channel_is_open = true;

  auto const encrypted = other.Encrypt(payload.dump());
  constexpr uint64_t msg_id = 9999;

  // First Delivery
  client_->GetMessagingService()->OnChatMessageReceived(peer1, encrypted, msg_id, false);

  // Check History
  auto page = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 2);
  ASSERT_EQ(page.messages[0].kind, messaging::HistoryEntryKind::systemMessage);
  ASSERT_EQ(page.messages[1].kind, messaging::HistoryEntryKind::textMessage);
  ASSERT_EQ(page.messages[1].remote_id, msg_id);

  // Clear transport log to check for receipt
  mock_transport_->log.sent_peer_messages.clear();

  // Second Delivery (Duplicate)
  client_->GetMessagingService()->OnChatMessageReceived(peer1, encrypted, msg_id, false);

  // Check History - Should still be 1text message
  page = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 2);
  ASSERT_EQ(page.messages[0].kind, messaging::HistoryEntryKind::systemMessage);
  ASSERT_EQ(page.messages[1].kind, messaging::HistoryEntryKind::textMessage);
  ASSERT_EQ(page.messages[1].remote_id, msg_id);

  // Check Receipt Sent
  bool receipt_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "message_receipt") {
      if (msg.value("message_id", 0ULL) == msg_id) {
        receipt_sent = true;
      }
    }
  }
  EXPECT_TRUE(receipt_sent);
}

TEST_F(ResilienceTest, SuspensionTimeout) {
  // Add a pending action with a very old timestamp (older than 2 days)
  uint64_t const old_time = Time::Now() - (48 * 3600 * 1000000ULL + 1000ULL);
  mock_storage_->AddPendingAction(peer1, "old_action", old_time, std::nullopt);

  // Verify it exists initially
  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(actions.size(), 1);

  // Trigger suspension check logic manually
  // threshold = Now - 2 days
  uint64_t const threshold = Time::Now() - (48 * 3600 * 1000000ULL);
  mock_storage_->SuspendTimedOutActions(threshold);

  // Verify it is suspended (filtered out)
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

TEST_F(ResilienceTest, UnsuspensionOnConnect) {
  // Setup suspended action
  // Use absolute small timestamp to guarantee it is older than threshold
  constexpr uint64_t old_time = 1000;
  mock_storage_->AddPendingAction(peer1, R"({"type": "test_suspend"})", old_time, std::nullopt);

  // Manually suspend it to simulate expired state
  // Threshold 2000 > 1000, so it will suspend
  mock_storage_->SuspendTimedOutActions(2000);

  // Verify it is suspended (should not be returned)
  ASSERT_TRUE(mock_storage_->GetPendingActions(peer1).empty());

  // Connect
  client_->Connect(peer1);
  FlushClientQueue();
  mock_transport_->log.sent_peer_messages.clear();

  // Simulate DataChannel open which triggers UnsuspendAndDeliver
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  // Action should have been unsuspended and sent
  bool action_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "test_suspend") {
      action_sent = true;
    }
  }
  ASSERT_TRUE(action_sent) << "Action was not sent after unsuspension";

  // And removed from pending
  auto const pending = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(pending.empty()) << "Action remained in pending queue after send";
}
