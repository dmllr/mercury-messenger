#include <cstring>
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
    file_transfer_service_.reset();  // Ensure files are closed before deletion
    std::filesystem::remove_all(temp_dir_);
  }

  // Helpers for binary protocol packet checks
  bool IsBinaryPacket(std::vector<std::byte> const& data, uint8_t type, std::string const& id) {
    if (data.size() < 3 + id.size()) return false;
    if (data[0] != std::byte{0xFE}) return false;
    if (data[1] != std::byte{type}) return false;
    if (static_cast<uint8_t>(data[2]) != id.size()) return false;
    std::string packet_id(id.size(), '\0');
    std::memcpy(packet_id.data(), data.data() + 3, id.size());
    return packet_id == id;
  }

  std::vector<std::byte> CreateChunkPacket(std::string const& id, uint32_t seq, std::vector<std::byte> const& payload) {
    std::vector<std::byte> packet;
    packet.push_back(std::byte{0xFE});
    packet.push_back(std::byte{0x01});  // Chunk Type
    packet.push_back(static_cast<std::byte>(id.size()));
    for (char c : id) packet.push_back(static_cast<std::byte>(c));

    // Seq LE
    for (int i = 0; i < 4; ++i) packet.push_back(static_cast<std::byte>((seq >> (i * 8)) & 0xFF));

    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
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
  // Setup connected peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // Create file to upload
  std::filesystem::path const file_path = temp_dir_ / "large_file.dat";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    // Write enough data to ensure it doesn't finish immediately
    std::string const data(
        services::FileTransferService::windowSizeChunks * services::FileTransferService::chunkSizeBytes * 2,
        'A'
    );
    ofs << data;
  }

  // Start Upload
  std::vector<services::AttachmentRequest> attachments;
  attachments.push_back({file_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});

  auto record = service_->SendChatMessage(peer1, "File", attachments);
  std::string const att_id = std::get<messaging::MessageEntry>(record.data).attachments[0].id;

  // Cancel
  service_->CancelTransfer(peer1, att_id, true);

  // 5. Verify cancel message sent (Binary protocol: Type 0x03)
  bool cancel_sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_binary_messages) {
    if (IsBinaryPacket(msg, 0x03, att_id)) {
      cancel_sent = true;
    }
  }
  EXPECT_TRUE(cancel_sent);

  // Verify DB status
  auto att_db = storage_->GetAttachment(att_id);
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::canceled);
}

TEST_F(FileTransferCancellationTest, CancelDownload_PausesDownload) {
  // Mock DB state for incoming attachment
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

  // Start Download by receiving chunk 0
  std::vector<std::byte> const payload = {std::byte{0x00}, std::byte{0x11}, std::byte{0x22}};
  auto chunk = CreateChunkPacket("down_id", 0, payload);

  file_transfer_service_->ProcessBinaryMessage(peer1, chunk);

  // Verify temp file exists
  std::filesystem::path const temp_path = attachments_dir_ / "down_id.tmp";
  ASSERT_TRUE(std::filesystem::exists(temp_path));

  // Cancel (is_upload = false) -> This acts as PAUSE for downloads
  service_->CancelTransfer(peer1, "down_id", false);

  // Verify PAUSE message sent (Binary Type 0x04)
  bool pause_sent = false;
  for (auto const& msg : mock_transport_->log.sent_binary_messages | std::views::values) {
    if (IsBinaryPacket(msg, 0x04, "down_id")) {  // 0x04 = PAUSE
      pause_sent = true;
    }
  }
  EXPECT_TRUE(pause_sent);

  // Verify temp file STILL exists (to allow resume)
  EXPECT_TRUE(std::filesystem::exists(temp_path));

  // Verify DB status is PAUSED
  auto att_db = storage_->GetAttachment("down_id");
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::paused);
}

TEST_F(FileTransferCancellationTest, ResumeSendsRequest) {
  // Setup peer
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  // Setup attachment in DB (upload)
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

  mock_transport_->log.sent_binary_messages.clear();

  // Resume
  service_->ResumeTransfer(peer1, "resume_id", false);

  // Verify resume request sent (Binary Type 0x05)
  bool req_sent = false;
  for (auto const& msg : mock_transport_->log.sent_binary_messages | std::views::values) {
    if (IsBinaryPacket(msg, 0x05, "resume_id")) {
      req_sent = true;
    }
  }
  EXPECT_TRUE(req_sent);
}
