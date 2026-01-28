#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

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
    client_->GetProfileService()->SetPeerListUpdateCallback([](auto const&) {});
    client_->GetProfileService()->SetProfileReceivedCallback([](auto const&, auto const&) {});

    client_->GetMediaCallService()->SetIncomingCallCallback([](auto const&, auto) {});
    client_->GetMediaCallService()->SetCallRingingCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallAcceptedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallTerminatedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetFrameReceivedCallback([](auto const&) {});

    client_->GetMessagingService()->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    client_->GetMessagingService()->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});

    client_->SetStatusCallback([](auto const&, auto, auto) {});

    (void)client_->GetAccountService()->GenerateNewIdentity();

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
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->UpdateMyProfile("New Name", "Bio", "");
  FlushClientQueue();

  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_FALSE(actions.empty());
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

TEST_F(ResilienceTest, PendingActionsPersistAndSend) {
  mock_transport_->RemovePeer(peer1);
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  client_->ResetContactDisplayNameOverride(peer1);
  FlushClientQueue();

  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_FALSE(actions.empty());
  bool found_req = false;
  for (auto const& act : actions) {
    if (act.payload.find("profile_request") != std::string::npos) {
      found_req = true;
    }
  }
  EXPECT_TRUE(found_req);

  client_->Connect(peer1);
  FlushClientQueue();

  mock_transport_->log.sent_peer_messages.clear();
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  FlushClientQueue();

  bool req_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "profile_request") {
      req_sent = true;
    }
  }
  EXPECT_TRUE(req_sent);

  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

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
      if (msg["type"] == "contact_signature") signature_sent = true;
      if (msg["type"] == "profile_info") profile_sent = true;
    }
  }
  EXPECT_TRUE(signature_sent) << "Should send signature via data channel";
  EXPECT_TRUE(profile_sent) << "Should send profile via data channel";
}
