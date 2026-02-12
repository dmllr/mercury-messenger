#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/internal_messaging_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

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

class InternalMessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // Transport
    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    media_transport_ = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    // Dependencies
    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);

    // Services
    account_service_ = std::make_shared<services::AccountService>(storage_, transport_);
    account_service_->SetOnMyProfileUpdatedCallback([]() {});

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    auto conn_handler = [](ContactID const&) {};

    messaging_service_ = std::make_shared<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        conn_handler
    );

    // Initialize dummy callbacks for MessagingService
    messaging_service_->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    messaging_service_->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});
    messaging_service_->SetChatMessageReceivedCallback([](auto const&, auto const&) {});

    media_call_service_ =
        std::make_shared<services::MediaCallService>(storage_, transport_, media_transport_, account_service_);
    media_call_service_->SetVideoCapture(std::make_unique<MockCapture>());
    media_call_service_->SetAudioCapture(std::make_unique<MockCapture>());

    profile_service_ = std::make_shared<services::ProfileService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_,
        messaging_service_,
        conn_handler
    );

    contact_service_ = std::make_shared<services::ContactService>(
        storage_,
        transport_,
        contact_manager_,
        account_service_,
        profile_service_,
        conn_handler
    );

    // Wire up circular dependencies roughly
    messaging_service_->SetContactRestoredCallback([&](auto id) { contact_service_->OnContactRestored(id); });
    messaging_service_->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});
    contact_service_->SetMessagingService(messaging_service_);
    media_call_service_->SetMessagingService(messaging_service_);

    // Satisfy invariants
    std::ignore = account_service_->GenerateNewIdentity();

    // Target
    service_ = std::make_unique<services::InternalMessagingService>(
        transport_,
        media_transport_,
        account_service_,
        contact_service_,
        messaging_service_,
        media_call_service_,
        file_transfer_service_,
        profile_service_
    );
  }

  void TearDown() override {
    messaging_service_->StopDeliveryService();
    file_transfer_service_.reset();  // Close handles
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;

  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::ContactService> contact_service_;
  std::shared_ptr<services::MessagingService> messaging_service_;
  std::shared_ptr<services::MediaCallService> media_call_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::shared_ptr<services::ProfileService> profile_service_;

  std::unique_ptr<services::InternalMessagingService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(InternalMessagingServiceTest, ProcessSystemMessage_AuthChallenge) {
  std::string challenge = "random_challenge_string";
  nlohmann::json const msg = {{"challenge", challenge}};

  service_->ProcessSystemMessage("auth_challenge", msg);

  ASSERT_EQ(mock_transport_->CallLog()->sent_signaling_messages.size(), 1);
  auto sent = mock_transport_->CallLog()->sent_signaling_messages[0];
  EXPECT_EQ(sent["type"], "auth_response");
  EXPECT_FALSE(sent["signature"].get<std::string>().empty());
}

TEST_F(InternalMessagingServiceTest, ProcessSystemMessage_LoginSuccess) {
  service_->ProcessSystemMessage("login_success", {});
}

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_DataOffer) {
  // Ensure we are the "passive" side (smaller ID or just accepting)
  nlohmann::json const offer = {{"from", peer1.ToString()}, {"context", "data"}, {"sdp", "fake_sdp"}};

  service_->ProcessSignalingMessage(peer1, "offer", offer);

  // Should trigger transport to set remote description and create answer
  bool answer_created = false;
  for (auto const& [pid, ctx] : mock_transport_->CallLog()->answers) {
    if (pid == peer1 && ctx == "data") {
      answer_created = true;
    }
  }
  EXPECT_TRUE(answer_created);
}

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_ContactAddRequest) {
  bool request_received = false;
  contact_service_->SetContactRequestCallback([&](auto id) {
    if (id == peer1) {
      request_received = true;
    }
  });

  nlohmann::json const msg = {{"profile", {{"display_name", "Peer 1"}}}};

  service_->ProcessSignalingMessage(peer1, "contact_add_request", msg);
  EXPECT_TRUE(request_received);
}

// DataChannel handlers tests still use `ProcessDataChannelMessage` directly which expects JSON.
// This remains valid for JSON-based messages (chat, keys).
TEST_F(InternalMessagingServiceTest, ProcessDataChannelMessage_Chat) {
  // Setup Peer with crypto
  auto peer = std::make_shared<transport::Peer>(peer1);
  transport_->AddPeer(peer1, peer);

  // Handshake crypto
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());

  // Prepare Encrypted Message (JSON Protocol)
  std::string const content = "Hello";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = content;

  auto enc = other.Encrypt(payload_json.dump());

  nlohmann::json const chat_msg = {
      {"type", "chat"},
      {"message_id", 100},
      {"is_system", false},
      {"payload", enc},
  };

  bool msg_received = false;
  bool contact_status_changed = false;
  messaging_service_->SetChatMessageReceivedCallback([&](auto id, auto) { msg_received = id == peer1; });
  contact_service_->SetContactStatusChangedCallback([&](auto id, auto) { contact_status_changed = id == peer1; });

  service_->ProcessDataChannelMessage(peer1, chat_msg);
  EXPECT_TRUE(msg_received);
  EXPECT_TRUE(contact_status_changed);
}

TEST_F(InternalMessagingServiceTest, ProcessDataChannelMessage_KeyExchange) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  transport_->AddPeer(peer1, peer);

  eillance::crypto::SessionCipher const other;
  auto pub = other.GetPublicKey();

  nlohmann::json const key_msg = {{"type", "key_exchange"}, {"key", pub}, {"is_reply", false}};

  service_->ProcessDataChannelMessage(peer1, key_msg);

  EXPECT_TRUE(peer->session_cipher->IsReady());

  // Should send reply
  bool reply_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "key_exchange" && msg["is_reply"] == true) {
      reply_sent = true;
    }
  }
  EXPECT_TRUE(reply_sent);
}

TEST_F(InternalMessagingServiceTest, ProcessDataChannelMessage_ProfileInfo) {
  bool profile_updated = false;
  bool message_received = false;
  bool contact_status_changed = false;

  profile_service_->SetContactProfileUpdatedCallback([&](auto id) { profile_updated = id == peer1; });
  messaging_service_->SetChatMessageReceivedCallback([&](auto id, auto const& rec) {
    if (id == peer1 && rec.kind == messaging::HistoryEntryKind::systemMessage) {
      message_received = true;
    }
  });
  contact_service_->SetContactStatusChangedCallback([&](auto id, auto) { contact_status_changed = id == peer1; });

  // Pre-add contact so update works
  contact_service_->AddContact(peer1, std::nullopt);

  nlohmann::json const msg = {{"type", "profile_info"}, {"display_name", "New Name"}};

  service_->ProcessDataChannelMessage(peer1, msg);
  EXPECT_TRUE(profile_updated);
  EXPECT_TRUE(message_received);
  EXPECT_TRUE(contact_status_changed);
}

TEST_F(InternalMessagingServiceTest, SetupPeerHandlers_BinaryRouting) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  service_->SetupPeerHandlers(peer, peer1);

  // Simulate binary packet (Magic 0xFE)
  std::vector<std::byte> bin_data = {std::byte{0xFE}, std::byte{0x01}};

  // We can't easily spy on FileTransferService::ProcessBinaryMessage without mocking it,
  // but we can ensure no crash and logical routing.
  peer->on_data_channel_message(bin_data);

  // Simulate JSON packet (converted from bytes)
  nlohmann::json const json_msg = {{"type", "test_msg"}};
  std::string str = json_msg.dump();
  std::vector<std::byte> json_bytes(str.size());
  std::ranges::transform(str, json_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });

  peer->on_data_channel_message(json_bytes);
}

TEST_F(InternalMessagingServiceTest, UnsuspendAndDeliver_RateLimited) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  service_->SetupPeerHandlers(peer, peer1);

  // Use an empty binary packet (or minimal valid) to trigger the handler
  std::vector<std::byte> msg = {std::byte{0xFE}, std::byte{0x00}};

  // Check initial count
  EXPECT_EQ(mock_storage_->unsuspend_call_count, 0);

  // 1. First packet should trigger unsuspend
  peer->on_data_channel_message(msg);
  EXPECT_EQ(mock_storage_->unsuspend_call_count, 1);

  // 2. Immediate second packet should be rate-limited and NOT trigger unsuspend
  peer->on_data_channel_message(msg);
  EXPECT_EQ(mock_storage_->unsuspend_call_count, 1);
}
