#include <gtest/gtest.h>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class MessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
    // Add contact so we can message them
    contact_manager_->AddContact(peer1);
    contact_manager_->MarkAsConfirmed(peer1);

    auto conn_handler = [this](ContactID const& id) { connection_requests_.push_back(id); };

    service_ = std::make_unique<services::MessagingService>(storage_, transport_, contact_manager_, conn_handler);
    service_->SetChatMessageReceivedCallback([](auto, auto) {});
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<services::MessagingService> service_;
  std::vector<ContactID> connection_requests_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(MessagingServiceTest, SendMessage_NoConnection_QueuesAndRequestsConnection) {
  // Ensure no peer connected
  ASSERT_FALSE(mock_transport_->GetPeer(peer1));

  auto const entry = service_->SendChatMessage(peer1, "Hello");

  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);

  // Verify queued in storage
  auto const pending = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::sending);
  ASSERT_EQ(pending.size(), 1);
  EXPECT_EQ(pending[0].id, entry.id);

  // Verify connection requested
  EXPECT_EQ(connection_requests_.size(), 1);
  EXPECT_EQ(connection_requests_[0], peer1);
}

TEST_F(MessagingServiceTest, SendMessage_Connected_SendsImmediately) {
  // Setup connected peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  // Simulate key exchange done
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  auto entry = service_->SendChatMessage(peer1, "Hello");

  // Verify not pending in storage (should be sent)
  auto pending = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::sending);
  EXPECT_TRUE(pending.empty());

  // Check storage status update
  auto history = service_->GetHistoryPage(peer1, 0, 1);
  ASSERT_FALSE(history.messages.empty());
  EXPECT_EQ(history.messages[0].status, messaging::MessageStatus::sent);

  // Verify transport sent data
  ASSERT_EQ(mock_transport_->log.sent_peer_messages.size(), 1);
  auto const& [pid, json_msg] = mock_transport_->log.sent_peer_messages[0];
  EXPECT_EQ(pid, peer1);
  EXPECT_EQ(json_msg["type"], "chat");
}

TEST_F(MessagingServiceTest, OnChatMessageReceived_ValidCurrentVersion) {
  // Setup peer with crypto
  auto const peer = std::make_shared<transport::Peer>(peer1);
  eillance::crypto::SessionCipher other;  // acts as remote
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  transport_->AddPeer(peer1, peer);

  std::string plain = "Secret Message";
  std::string payload_with_version;
  payload_with_version.reserve(sizeof(services::MessagingService::currentMessageVersion) + plain.size());
  // Prepend the CURRENT version
  payload_with_version.push_back(static_cast<char>(services::MessagingService::currentMessageVersion));
  payload_with_version.append(plain);
  auto const encrypted = other.Encrypt(payload_with_version);

  bool cb_called = false;
  service_->SetChatMessageReceivedCallback([&](auto const& id, auto const& rec) {
    if (id == peer1 && std::get<messaging::MessageEntry>(rec.data).content == plain) {
      cb_called = true;
    }
  });

  service_->OnChatMessageReceived(peer1, encrypted, 12345, false);

  EXPECT_TRUE(cb_called);

  // Verify receipt sent
  bool receipt_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "message_receipt" && msg["message_id"] == 12345) {
      receipt_sent = true;
    }
  }
  EXPECT_TRUE(receipt_sent);
}

TEST_F(MessagingServiceTest, OnChatMessageReceived_UnsupportedVersion) {
  // Setup peer with crypto
  auto const peer = std::make_shared<transport::Peer>(peer1);
  crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  uint32_t const future_version = services::MessagingService::currentMessageVersion + 1;
  std::string const plain_content = "This content should not be seen";

  std::string payload_with_version;
  payload_with_version.push_back(static_cast<char>(future_version));
  payload_with_version.append(plain_content);

  auto const encrypted = other.Encrypt(payload_with_version);

  bool cb_called = false;
  service_->SetChatMessageReceivedCallback([&](auto const&, auto const&) { cb_called = true; });

  service_->OnChatMessageReceived(peer1, encrypted, 123, false);

  // Callback should be called to notify the UI of the new system message
  EXPECT_TRUE(cb_called);

  // Verify that a system message with an error was stored
  auto history = service_->GetHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(history.messages.empty());
  bool error_message_found = false;
  for (auto const& msg : history.messages) {
    if (msg.kind == messaging::HistoryEntryKind::systemMessage) {
      auto const& content = std::get<messaging::MessageEntry>(msg.data).content;
      if (content.find("Unsupported message of version") != std::string::npos) {
        error_message_found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(error_message_found);

  // A receipt should still be sent
  bool receipt_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "message_receipt" && msg["message_id"] == 123) {
      receipt_sent = true;
    }
  }
  EXPECT_TRUE(receipt_sent);
}

TEST_F(MessagingServiceTest, OnChatMessageReceived_OlderVersion) {
  // This test is valid if currentMessageVersion > 0
  if (services::MessagingService::currentMessageVersion == 0) {
    GTEST_SKIP() << "Skipping older version test as current version is 0.";
  }

  // Setup peer with crypto
  auto const peer = std::make_shared<transport::Peer>(peer1);
  crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  uint32_t const older_version = 0;
  std::string const plain_content = "Message from the past";

  std::string payload_with_version;
  payload_with_version.push_back(static_cast<char>(older_version));
  payload_with_version.append(plain_content);

  auto const encrypted = other.Encrypt(payload_with_version);

  bool cb_called = false;
  service_->SetChatMessageReceivedCallback([&](auto const& id, auto const& rec) {
    if (id == peer1 && rec.kind == messaging::HistoryEntryKind::textMessage &&
        std::get<messaging::MessageEntry>(rec.data).content == plain_content) {
      cb_called = true;
    }
  });

  service_->OnChatMessageReceived(peer1, encrypted, 456, false);

  // The message should be processed normally
  EXPECT_TRUE(cb_called);

  // Verify the stored message is correct and not a system error
  auto history = service_->GetHistoryPage(peer1, 0, 10);
  bool message_found = false;
  for (auto const& msg : history.messages) {
    if (msg.remote_id.has_value() && *msg.remote_id == 456) {
      EXPECT_EQ(msg.kind, messaging::HistoryEntryKind::textMessage);
      EXPECT_EQ(std::get<messaging::MessageEntry>(msg.data).content, plain_content);
      message_found = true;
      break;
    }
  }
  EXPECT_TRUE(message_found);
}

TEST_F(MessagingServiceTest, OnChatMessageReceived_DecryptionFail) {
  // Setup peer
  auto const peer = std::make_shared<transport::Peer>(peer1);
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<std::byte> const garbage(50, std::byte{0x00});

  bool cb_called = false;
  service_->SetChatMessageReceivedCallback([&](auto, auto) { cb_called = true; });

  service_->OnChatMessageReceived(peer1, garbage, 12345, false);

  EXPECT_FALSE(cb_called);
  // No receipt should be sent
  EXPECT_TRUE(mock_transport_->log.sent_peer_messages.empty());
}

TEST_F(MessagingServiceTest, MarkMessagesAsRead_SendsRemoteIds) {
  // Setup connected peer
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  // 1. Receive a message with a specific remote ID
  constexpr uint64_t remote_msg_id = 8888;

  eillance::crypto::SessionCipher other;
  // Perform key exchange both ways so 'other' can encrypt and 'peer' can decrypt
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());

  std::string const plain = "Content";
  std::string payload_with_version;
  payload_with_version.reserve(sizeof(services::MessagingService::currentMessageVersion) + plain.size());
  payload_with_version.push_back(static_cast<char>(services::MessagingService::currentMessageVersion));
  payload_with_version.append(plain);
  auto const encrypted = other.Encrypt(payload_with_version);

  service_->OnChatMessageReceived(peer1, encrypted, remote_msg_id, false);

  // Verify message added and get its local ID
  auto [messages, total_messages] = service_->GetHistoryPage(peer1, 0, 1);
  ASSERT_FALSE(messages.empty());
  uint64_t const local_id = messages[0].id;

  // 2. Mark as read
  service_->MarkMessagesAsRead(peer1, {local_id});

  // 3. Verify the read receipt sent contains the REMOTE ID (8888), not local ID
  ASSERT_FALSE(mock_transport_->log.sent_peer_messages.empty());

  bool read_receipt_found = false;
  for (auto const& [pid, json] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && json["type"] == "message_read") {
      auto ids = json["message_ids"].get<std::vector<uint64_t>>();
      if (ids.size() == 1 && ids[0] == remote_msg_id) {
        read_receipt_found = true;
      }
    }
  }
  EXPECT_TRUE(read_receipt_found);
}
