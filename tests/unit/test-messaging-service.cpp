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

  auto entry = service_->SendChatMessage(peer1, "Hello");

  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);

  // Verify queued in storage
  auto pending = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::sending);
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

TEST_F(MessagingServiceTest, OnChatMessageReceived_Valid) {
  // Setup peer with crypto
  auto peer = std::make_shared<transport::Peer>(peer1);
  eillance::crypto::SessionCipher other;  // acts as remote
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  transport_->AddPeer(peer1, peer);

  std::string plain = "Secret Message";
  auto const encrypted = other.Encrypt(plain);

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

TEST_F(MessagingServiceTest, OnChatMessageReceived_DecryptionFail) {
  // Setup peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<std::byte> garbage(50, std::byte{0x00});

  bool cb_called = false;
  service_->SetChatMessageReceivedCallback([&](auto, auto) { cb_called = true; });

  service_->OnChatMessageReceived(peer1, garbage, 12345, false);

  EXPECT_FALSE(cb_called);
  // No receipt should be sent
  EXPECT_TRUE(mock_transport_->log.sent_peer_messages.empty());
}

TEST_F(MessagingServiceTest, MarkMessagesAsRead_SendsRemoteIds) {
  // Setup connected peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  // 1. Receive a message with a specific remote ID
  constexpr uint64_t remote_msg_id = 8888;

  eillance::crypto::SessionCipher other;
  // Perform key exchange both ways so 'other' can encrypt and 'peer' can decrypt
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());

  std::string plain = "Content";
  auto encrypted = other.Encrypt(plain);

  service_->OnChatMessageReceived(peer1, encrypted, remote_msg_id, false);

  // Verify message added and get its local ID
  auto page = service_->GetHistoryPage(peer1, 0, 1);
  ASSERT_FALSE(page.messages.empty());
  uint64_t local_id = page.messages[0].id;

  // 2. Mark as read
  service_->MarkMessagesAsRead(peer1, {local_id});

  // 3. Verify the read receipt sent contains the REMOTE ID (8888), not local ID
  ASSERT_FALSE(mock_transport_->log.sent_peer_messages.empty());

  bool read_receipt_found = false;
  for (auto const& [pid, json] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && json["type"] == "message_read") {
      std::vector<uint64_t> ids = json["message_ids"].get<std::vector<uint64_t>>();
      if (ids.size() == 1 && ids[0] == remote_msg_id) {
        read_receipt_found = true;
      }
    }
  }
  EXPECT_TRUE(read_receipt_found);
}
