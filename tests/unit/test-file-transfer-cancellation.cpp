#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class FileTransferCancellationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "cancel_tests";
    std::filesystem::create_directories(temp_dir_);

    attachments_dir_ = temp_dir_ / "attachments";
    std::filesystem::create_directories(attachments_dir_);

    std::filesystem::path db_path = temp_dir_ / "db.sqlite";

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>(db_path.string());
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, transport_, media_transport);

    std::ignore = client_->GenerateNewIdentity();
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
    std::filesystem::remove_all(temp_dir_);
  }

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
    for (char c : id) {
      packet.push_back(static_cast<std::byte>(c));
    }

    for (int i = 0; i < 4; ++i) {
      packet.push_back(static_cast<std::byte>((seq >> (i * 8)) & 0xFF));
    }

    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path attachments_dir_;
  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(FileTransferCancellationTest, CancelUpload) {
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::filesystem::path const file_path = temp_dir_ / "large_file.dat";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    std::string const data(16 * 16384 * 2, 'A');
    ofs << data;
  }

  std::vector<messaging::AttachmentRequest> attachments;
  attachments.push_back({file_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});

  auto record = client_->SendChatMessage(peer1, "File", attachments);
  std::string const att_id = std::get<messaging::MessageEntry>(record.data).attachments[0].id;
  EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_binary_messages) {
          if (IsBinaryPacket(msg, 0x01, att_id)) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  client_->CancelTransfer(peer1, att_id, true);
  EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_binary_messages) {
          if (IsBinaryPacket(msg, 0x03, att_id)) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool cancel_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_binary_messages) {
    if (IsBinaryPacket(msg, 0x03, att_id)) {
      cancel_sent = true;
    }
  }
  EXPECT_TRUE(cancel_sent);

  auto att_db = storage_->GetAttachment(att_id);
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::canceled);
}

TEST_F(FileTransferCancellationTest, CancelDownload_PausesDownload) {
  messaging::Attachment att;
  att.id = "down_id";
  att.type = messaging::AttachmentType::file;
  att.size_bytes = 100000;
  att.file_path = (attachments_dir_ / "down.dat").string();
  att.hash = "dummy_hash";
  att.is_downloaded = false;

  messaging::MessageEntry msg_data;
  msg_data.attachments.push_back(att);

  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::received,
       .data = msg_data,
       .peer_id = peer1}
  );

  std::vector<std::byte> const payload = {std::byte{0x00}, std::byte{0x11}, std::byte{0x22}};
  auto chunk = CreateChunkPacket("down_id", 0, payload);

  EventQueue::Instance().Push(EventDataChannelMessage{peer1, chunk});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  std::filesystem::path const temp_path = attachments_dir_ / "down_id.tmp";
  ASSERT_TRUE(std::filesystem::exists(temp_path));

  client_->CancelTransfer(peer1, "down_id", false);
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  bool pause_sent = false;
  for (auto const& msg : mock_transport_->CallLog()->sent_binary_messages | std::views::values) {
    if (IsBinaryPacket(msg, 0x04, "down_id")) {
      pause_sent = true;
    }
  }
  EXPECT_TRUE(pause_sent);
  EXPECT_TRUE(std::filesystem::exists(temp_path));

  auto att_db = storage_->GetAttachment("down_id");
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::paused);
}

TEST_F(FileTransferCancellationTest, ResumeSendsRequest) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  messaging::Attachment att;
  att.id = "resume_id";
  att.is_downloaded = true;
  att.file_path = (temp_dir_ / "upload.dat").string();
  att.size_bytes = 100;
  att.hash = "abc";

  std::ofstream(att.file_path) << "data";

  messaging::MessageEntry msg_data;
  msg_data.attachments.push_back(att);
  std::ignore = storage_->AddHistoryEntry({.data = msg_data, .peer_id = peer1});

  {
    std::lock_guard<std::mutex> const lock(mock_transport_->CallLog()->mutex_);
    mock_transport_->CallLog()->sent_binary_messages.clear();
  }

  client_->ResumeTransfer(peer1, "resume_id", false);
  EventQueue::Instance().WaitFor(
      [&] {
        std::lock_guard<std::mutex> const lock(mock_transport_->CallLog()->mutex_);
        for (auto const& msg : mock_transport_->CallLog()->sent_binary_messages | std::views::values) {
          if (IsBinaryPacket(msg, 0x05, "resume_id")) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool req_sent = false;
  for (auto const& msg : mock_transport_->CallLog()->sent_binary_messages | std::views::values) {
    if (IsBinaryPacket(msg, 0x05, "resume_id")) {
      req_sent = true;
    }
  }
  EXPECT_TRUE(req_sent);
}
