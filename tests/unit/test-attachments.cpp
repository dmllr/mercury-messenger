#include <filesystem>
#include <fstream>
#include <variant>

#include <gtest/gtest.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class AttachmentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup temp file for hashing tests
    temp_file_path_ = "test_image.jpg";
    std::ofstream ofs(temp_file_path_, std::ios::binary);
    ofs << "fake image data";
    ofs.close();

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    auto conn_handler = [](ContactID const&) {};

    service_ = std::make_unique<services::MessagingService>(storage_, transport_, contact_manager, conn_handler);

    // Satisfy invariants required by MessagingService
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
    service_->SetChatMessageReceivedCallback([](auto, auto) {});

    contact_manager->AddContact(peer1);
  }

  void TearDown() override {
    std::filesystem::remove(temp_file_path_);
    // Cleanup any extra files
    std::filesystem::remove("test_bad_file");
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<transport::Transport> transport_;
  std::unique_ptr<services::MessagingService> service_;
  std::string temp_file_path_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(AttachmentTest, SendMessageCalculatesHash) {
  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({temp_file_path_, messaging::AttachmentType::image, "base64thumb", "image/jpeg"});

  auto const record = service_->SendChatMessage(peer1, "Check this out", attachments);

  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(record.data));
  auto const& entry = std::get<messaging::MessageEntry>(record.data);
  ASSERT_EQ(entry.attachments.size(), 1);

  auto const& att = entry.attachments[0];
  EXPECT_EQ(att.file_path, temp_file_path_);
  EXPECT_EQ(att.mime_type, "image/jpeg");
  EXPECT_FALSE(att.hash.empty());
  EXPECT_EQ(att.hash.length(), 64);
}

TEST_F(AttachmentTest, PersistenceStoresHash) {
  messaging::MessageEntry msg_data;
  msg_data.content = "Text";
  msg_data.attachments.push_back(
      {"uuid-123", messaging::AttachmentType::video, "/data/vid.mp4", "video/mp4", 1024, "", "abc123hash", true}
  );

  messaging::HistoryEntryRecord record{
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = 1000,
      .direction = messaging::MessageDirection::sent,
      .data = msg_data,
      .peer_id = peer1
  };

  std::ignore = storage_->AddHistoryEntry(record);

  // Retrieve
  auto [messages, total_messages] = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(messages.size(), 1);

  auto const& retrieved = messages[0];
  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(retrieved.data));
  auto const& [content, attachments] = std::get<messaging::MessageEntry>(retrieved.data);

  ASSERT_EQ(attachments.size(), 1);
  EXPECT_EQ(attachments[0].id, "uuid-123");
  EXPECT_EQ(attachments[0].type, messaging::AttachmentType::video);
  EXPECT_EQ(attachments[0].hash, "abc123hash");
}

TEST_F(AttachmentTest, SendQueuedMessagesSerializesHash) {
  // Setup peer with open data channel
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({temp_file_path_, messaging::AttachmentType::image, "", "image/jpeg"});

  service_->SendChatMessage(peer1, "Text", attachments);

  // Check transport log for the CHAT message (ignoring file chunks)
  bool found_chat_message = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (msg.contains("type") && msg["type"] == "chat" && msg.contains("attachments")) {
      auto hash = msg["attachments"][0]["hash"].get<std::string>();
      EXPECT_FALSE(hash.empty());
      EXPECT_EQ(hash.length(), 64);
      found_chat_message = true;
      break;
    }
  }
  ASSERT_TRUE(found_chat_message) << "Chat message with attachments not found in transport log";
}

TEST_F(AttachmentTest, AttachmentWithComputationFailureIsSkipped) {
  // Use a non-existent file to force processing failure.
  // std::filesystem::file_size throws if file does not exist, triggering the catch block which skips the attachment.
  std::string const bad_file = "non_existent_file_guid_12345.dat";

  // Ensure it doesn't exist
  if (std::filesystem::exists(bad_file)) {
    std::filesystem::remove(bad_file);
  }

  std::vector<services::AttachmentRequest> attachments;
  // Use generic mime type to rely on file processing
  attachments.push_back({bad_file, messaging::AttachmentType::file, "", "application/octet-stream"});

  // We expect this to NOT add the attachment because validation/processing fails
  auto const record = service_->SendChatMessage(peer1, "msg", attachments);

  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(record.data));
  auto const& entry = std::get<messaging::MessageEntry>(record.data);

  // Attachment should be skipped because file_size throws or hash calculation fails
  EXPECT_TRUE(entry.attachments.empty());
}
