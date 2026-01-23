#include <gtest/gtest.h>
#include <variant>

#include "eillance/messaging/history.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class AttachmentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    // Mock dependencies for service
    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
    auto conn_handler = [](ContactID const&){};

    service_ = std::make_unique<services::MessagingService>(
        storage_, transport_, contact_manager, conn_handler
    );

    // Ensure peer is a contact
    contact_manager->AddContact(peer1);
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  std::shared_ptr<transport::Transport> transport_;
  std::unique_ptr<services::MessagingService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(AttachmentTest, SendMessageWithAttachments) {
  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({"/tmp/image.jpg", messaging::AttachmentType::image, "base64thumb"});

  auto record = service_->SendChatMessage(peer1, "Check this out", attachments);

  // Verify Record
  EXPECT_NE(record.id, 0);
  EXPECT_TRUE(std::holds_alternative<messaging::MessageEntry>(record.data));

  auto const& entry = std::get<messaging::MessageEntry>(record.data);
  EXPECT_EQ(entry.content, "Check this out");
  ASSERT_EQ(entry.attachments.size(), 1);

  auto const& att = entry.attachments[0];
  EXPECT_EQ(att.file_path, "/tmp/image.jpg");
  EXPECT_EQ(att.type, messaging::AttachmentType::image);
  EXPECT_EQ(att.thumbnail_base64, "base64thumb");
  EXPECT_TRUE(att.is_downloaded); // Local sender has file
}

TEST_F(AttachmentTest, PersistenceAndRetrieval) {
  // Simulate DB directly to test storage layer
  messaging::MessageEntry msg_data;
  msg_data.content = "Text";
  msg_data.attachments.push_back({
      "uuid-123", messaging::AttachmentType::video, "/data/vid.mp4", "video/mp4", 1024, "", true
  });

  messaging::HistoryEntryRecord record{
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = 1000,
      .direction = messaging::MessageDirection::sent,
      .data = msg_data,
      .peer_id = peer1
  };

  storage_->AddHistoryEntry(record);

  // Retrieve
  auto page = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 1);

  auto const& retrieved = page.messages[0];
  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(retrieved.data));
  auto const& retrieved_msg = std::get<messaging::MessageEntry>(retrieved.data);

  ASSERT_EQ(retrieved_msg.attachments.size(), 1);
  EXPECT_EQ(retrieved_msg.attachments[0].id, "uuid-123");
  EXPECT_EQ(retrieved_msg.attachments[0].type, messaging::AttachmentType::video);
}
