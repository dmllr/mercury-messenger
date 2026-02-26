#include <filesystem>
#include <fstream>
#include <variant>

#include <gtest/gtest.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class AttachmentTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_file_path_ = "test_image.jpg";
    std::ofstream ofs(temp_file_path_, std::ios::binary);
    ofs << "fake image data";
    ofs.close();

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_data_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_data_transport_raw_ = mock_data_transport_impl_.get();
    data_transport_ = std::make_shared<transport::DataTransport>(std::move(mock_data_transport_impl_));

    account_service_ = std::make_shared<services::AccountService>(storage_, data_transport_);

    auto contact_manager = std::make_shared<identity::ContactManager>(storage_);

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, data_transport_);

    service_ = std::make_unique<services::MessagingService>(
        storage_,
        data_transport_,
        contact_manager,
        account_service_,
        file_transfer_service_
    );

    contact_manager->AddContact(peer1);
  }

  void TearDown() override {
    service_.reset();
    file_transfer_service_.reset();
    std::filesystem::remove(temp_file_path_);
    if (std::filesystem::exists("test_bad_file")) {
      std::filesystem::remove("test_bad_file");
    }
  }

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_data_transport_impl_;
  transport::MockDataTransport* mock_data_transport_raw_;
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<services::AccountService> account_service_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
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
      .status = messaging::MessageStatus::delivered,
      .data = msg_data,
      .peer_id = peer1
  };

  (void)storage_->AddHistoryEntry(record);

  auto page = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 1);

  auto const& retrieved = page.messages[0];
  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(retrieved.data));
  auto const& [content, attachments] = std::get<messaging::MessageEntry>(retrieved.data);

  ASSERT_EQ(attachments.size(), 1);
  EXPECT_EQ(attachments[0].id, "uuid-123");
  EXPECT_EQ(attachments[0].type, messaging::AttachmentType::video);
  EXPECT_EQ(attachments[0].hash, "abc123hash");
}

TEST_F(AttachmentTest, SendQueuedMessagesSerializesHash) {
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());
  data_transport_->AddPeer(peer1, peer);

  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({temp_file_path_, messaging::AttachmentType::image, "", "image/jpeg"});

  service_->SendChatMessage(peer1, "Text", attachments);

  bool found_chat_message = false;
  for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
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
  std::string const bad_file = "non_existent_file_guid_12345.dat";
  if (std::filesystem::exists(bad_file)) {
    std::filesystem::remove(bad_file);
  }

  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({bad_file, messaging::AttachmentType::file, "", "application/octet-stream"});

  auto const record = service_->SendChatMessage(peer1, "msg", attachments);

  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(record.data));
  auto const& entry = std::get<messaging::MessageEntry>(record.data);

  EXPECT_TRUE(entry.attachments.empty());
}

TEST_F(AttachmentTest, AttachmentPersistenceAcrossRestarts) {
  messaging::Attachment att;
  att.id = "restart_test_id";
  att.type = messaging::AttachmentType::image;
  att.size_bytes = 10;
  att.is_downloaded = false;
  att.mime_type = "image/jpeg";

  std::filesystem::path storage_root(storage_->GetPath());
  std::filesystem::path attachments_dir = storage_root.parent_path() / "attachments";
  std::filesystem::create_directories(attachments_dir);
  std::filesystem::path final_path = attachments_dir / (att.id + ".jpg");

  std::ofstream ofs(final_path, std::ios::binary);
  ofs << "0123456789";
  ofs.close();

  att.file_path = final_path.string();

  messaging::MessageEntry msg_data;
  msg_data.content = "Image";
  msg_data.attachments.push_back(att);

  messaging::HistoryEntryRecord record{
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = 1000,
      .direction = messaging::MessageDirection::received,
      .status = messaging::MessageStatus::delivered,
      .data = msg_data,
      .peer_id = peer1
  };

  (void)storage_->AddHistoryEntry(record);

  auto contact_manager = std::make_shared<identity::ContactManager>(storage_);
  auto file_transfer_service = std::make_shared<services::FileTransferService>(storage_, data_transport_);
  auto account_service = std::make_shared<services::AccountService>(storage_, data_transport_);

  auto new_service = std::make_unique<services::MessagingService>(
      storage_,
      data_transport_,
      contact_manager,
      account_service,
      file_transfer_service
  );

  auto page = new_service->GetHistoryPage(peer1, 0, 10);

  ASSERT_EQ(page.messages.size(), 1);
  auto const& loaded_msg = std::get<messaging::MessageEntry>(page.messages[0].data);
  ASSERT_EQ(loaded_msg.attachments.size(), 1);

  auto const& loaded_att = loaded_msg.attachments[0];
  EXPECT_TRUE(loaded_att.is_downloaded) << "Attachment should be marked downloaded because file exists";
  EXPECT_EQ(loaded_att.file_path, final_path.string());
}
