#include <filesystem>
#include <fstream>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class FileTransferCancellationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "cancel_tests";
    std::filesystem::create_directories(temp_dir_);

    // Attachments dir relative to mock DB path
    attachments_dir_ = temp_dir_ / "attachments";
    std::filesystem::create_directories(attachments_dir_);

    // Use a DB path that allows the service to find the attachments dir relative to it
    std::filesystem::path db_path = temp_dir_ / "db.sqlite";

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>(db_path.string());
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
    contact_manager_->AddContact(peer1);

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    auto conn_handler = [](ContactID const&) {};
    service_ = std::make_unique<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        conn_handler
    );

    // Satisfy invariants
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
    service_->SetChatMessageReceivedCallback([](auto, auto) {});
    service_->SetAttachmentDownloadedCallback([](auto, auto) {});
    service_->SetTransferProgressCallback([](auto, auto, auto) {});
    service_->SetAttachmentStatusChangedCallback([](auto, auto, auto) {});
  }

  void TearDown() override {
    service_->StopDeliveryService();
    service_.reset();
    file_transfer_service_.reset(); // Ensure files are closed before deletion
    std::filesystem::remove_all(temp_dir_);
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path attachments_dir_;
  storage::MockPersistentStorageImpl* mock_storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::unique_ptr<services::MessagingService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(FileTransferCancellationTest, CancelUpload) {
  // 1. Setup connected peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // 2. Create file to upload
  std::filesystem::path file_path = temp_dir_ / "large_file.dat";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    // Write enough data to ensure it doesn't finish in one pump
    std::string data(
        services::FileTransferService::windowSizeChunks * services::FileTransferService::chunkSizeBytes * 2,
        'A'
    );
    ofs << data;
  }

  // 3. Start Upload
  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({file_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});

  auto record = service_->SendChatMessage(peer1, "File", attachments);
  std::string att_id = std::get<messaging::MessageEntry>(record.data).attachments[0].id;

  // 4. Cancel
  service_->CancelTransfer(peer1, att_id, true);

  // 5. Verify cancel message sent
  bool cancel_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (msg["type"] == "file_cancel" && msg["id"] == att_id) {
      cancel_sent = true;
    }
  }
  EXPECT_TRUE(cancel_sent);

  // 6. Verify DB status
  auto att_db = storage_->GetAttachment(att_id);
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::canceled);
}

TEST_F(FileTransferCancellationTest, CancelDownload) {
  // 1. Mock DB state for incoming attachment
  messaging::Attachment att;
  att.id = "down_id";
  att.type = messaging::AttachmentType::file;
  att.size_bytes = 100000;
  att.file_path = (attachments_dir_ / "down.dat").string();
  att.hash = "dummy_hash";
  att.is_downloaded = false;

  messaging::MessageEntry msg_data;
  msg_data.attachments.push_back(att);

  std::ignore = storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::received,
       .data = msg_data,
       .peer_id = peer1}
  );

  // 2. Start Download by receiving chunk 0
  nlohmann::json chunk = {{"type", "file_chunk"}, {"id", "down_id"}, {"seq", 0}, {"data", "001122"}, {"eof", false}};
  file_transfer_service_->ProcessMessage(peer1, chunk);

  // Verify temp file exists
  std::filesystem::path temp_path = attachments_dir_ / "down_id.tmp";
  ASSERT_TRUE(std::filesystem::exists(temp_path));

  // 3. Cancel
  service_->CancelTransfer(peer1, "down_id", false);

  // 4. Verify cancel message sent
  bool cancel_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (msg["type"] == "file_cancel" && msg["id"] == "down_id") {
      cancel_sent = true;
    }
  }
  EXPECT_TRUE(cancel_sent);

  // 5. Verify temp file deleted
  EXPECT_FALSE(std::filesystem::exists(temp_path));

  // 6. Verify DB status
  auto att_db = storage_->GetAttachment("down_id");
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::canceled);
}

TEST_F(FileTransferCancellationTest, ResumeSendsRequest) {
  // 1. Setup peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  // 2. Setup attachment in DB (upload)
  messaging::Attachment att;
  att.id = "resume_id";
  att.is_downloaded = true;  // upload
  att.file_path = (temp_dir_ / "upload.dat").string();
  att.size_bytes = 100;
  att.hash = "abc";

  // Create the file
  std::ofstream(att.file_path) << "data";

  messaging::MessageEntry msg_data;
  msg_data.attachments.push_back(att);
  std::ignore = storage_->AddHistoryEntry({.data = msg_data, .peer_id = peer1});

  mock_transport_->log.sent_peer_messages.clear();

  // 3. Resume
  service_->ResumeTransfer(peer1, "resume_id", true);

  // 4. Verify resume request sent (StartUpload sends it)
  bool req_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (msg["type"] == "file_resume_request" && msg["id"] == "resume_id") {
      req_sent = true;
    }
  }
  EXPECT_TRUE(req_sent);
}
