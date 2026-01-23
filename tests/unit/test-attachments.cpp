#include <filesystem>
#include <fstream>
#include <variant>

#include <gtest/gtest.h>

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
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    auto conn_handler = [](ContactID const&) {};

    service_ = std::make_unique<services::MessagingService>(storage_, transport_, contact_manager, conn_handler);

    contact_manager->AddContact(peer1);
  }

  void TearDown() override { std::filesystem::remove(temp_file_path_); }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  std::shared_ptr<transport::Transport> transport_;
  std::unique_ptr<services::MessagingService> service_;
  std::string temp_file_path_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(AttachmentTest, SendMessageCalculatesHash) {
  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({temp_file_path_, messaging::AttachmentType::image, "base64thumb"});

  auto const record = service_->SendChatMessage(peer1, "Check this out", attachments);

  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(record.data));
  auto const& entry = std::get<messaging::MessageEntry>(record.data);
  ASSERT_EQ(entry.attachments.size(), 1);

  auto const& att = entry.attachments[0];
  EXPECT_EQ(att.file_path, temp_file_path_);
  // Ensure hash is not empty and looks like a hex string
  EXPECT_FALSE(att.hash.empty());
  EXPECT_EQ(att.hash.length(), 64);  // SHA-256 hex length
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
}
