#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

// A test class specifically for file transfer scenarios, including error cases
class FileTransferResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup temporary directory for file tests
    temp_dir_ = std::filesystem::temp_directory_path() / "ft_resilience_tests";
    std::filesystem::create_directories(temp_dir_);

    // We need a separate attachments dir
    attachments_dir_ = temp_dir_ / "attachments";
    std::filesystem::create_directories(attachments_dir_);

    // Mock storage that points to real temp directory
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>(temp_dir_.string() + "/db.sqlite");
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
    contact_manager_->AddContact(peer1);
    contact_manager_->MarkAsConfirmed(peer1);

    auto conn_handler = [](ContactID const&) {};

    service_ = std::make_unique<services::MessagingService>(storage_, transport_, contact_manager_, conn_handler);

    // Default dummy callbacks
    service_->SetChatMessageReceivedCallback([](auto, auto) {});
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
    service_->SetAttachmentDownloadedCallback([](auto, auto) {});
    service_->SetTransferProgressCallback([](auto, auto, auto) {});
  }

  void TearDown() override {
    service_->StopDeliveryService();
    service_.reset();
    std::filesystem::remove_all(temp_dir_);
  }

  static std::string CalculateSha256(std::string const& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);
    std::vector<std::byte> hash_bytes(SHA256_DIGEST_LENGTH);
    std::memcpy(hash_bytes.data(), hash, SHA256_DIGEST_LENGTH);
    return eillance::crypto::ToHex(hash_bytes);
  }

  // Helper to convert std::string to std::vector<std::byte>
  static std::vector<std::byte> StringToBytes(std::string const& str) {
    std::vector<std::byte> bytes(str.size());
    std::transform(str.begin(), str.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path attachments_dir_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<services::MessagingService> service_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(FileTransferResilienceTest, TransferProgressAndFileCreation) {
  // Setup peer with crypto
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other_cipher;
  peer->session_cipher->ComputeSharedSecret(other_cipher.GetPublicKey());
  other_cipher.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // Capture progress events
  std::vector<float> progress_events;
  service_->SetTransferProgressCallback([&](auto, auto, float p) { progress_events.push_back(p); });

  bool completed = false;
  service_->SetAttachmentDownloadedCallback([&](auto, auto) { completed = true; });

  // 1. Receive Chat Message with Attachment
  std::string content = "Here is a large file";
  std::string versioned_payload;
  versioned_payload.push_back(static_cast<char>(services::MessagingService::currentMessageVersion));
  versioned_payload.append(content);
  auto encrypted = other_cipher.Encrypt(versioned_payload);

  std::string file_data(20000, 'A');  // Larger than one chunk (16KB)
  std::string file_hash = CalculateSha256(file_data);
  std::string att_id = "test_file_id";

  messaging::Attachment att;
  att.id = att_id;
  att.type = messaging::AttachmentType::file;
  att.mime_type = "text/plain";
  att.size_bytes = file_data.size();
  att.hash = file_hash;
  att.is_downloaded = false;

  // This should create the temp file and reserve space
  service_->OnChatMessageReceived(peer1, encrypted, 9001, false, {att});

  // Verify temp file created
  std::filesystem::path temp_file = attachments_dir_ / (att_id + ".tmp");
  ASSERT_TRUE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(temp_file), file_data.size());

  // 2. Process Chunks
  // Service uses 4KB chunks (MessagingService::chunkSizeBytes)
  // We must match this size for the offset calculation (seq * chunk_size) to align with data written.
  size_t chunk_size = 4 * 1024;
  size_t total_chunks = (file_data.size() + chunk_size - 1) / chunk_size;

  for (size_t seq = 0; seq < total_chunks; ++seq) {
    size_t offset = seq * chunk_size;
    std::string chunk = file_data.substr(offset, chunk_size);
    nlohmann::json msg = {
        {"type", "file_chunk"},
        {"id", att_id},
        {"seq", seq},
        {"data", eillance::crypto::ToHex(StringToBytes(chunk))},
        {"eof", false}
    };
    service_->ProcessIncomingFileChunk(peer1, msg);
  }

  // 3. EOF
  nlohmann::json eof_msg = {{"type", "file_chunk"}, {"id", att_id}, {"seq", 2}, {"data", ""}, {"eof", true}};
  service_->ProcessIncomingFileChunk(peer1, eof_msg);

  // Verify completion
  EXPECT_TRUE(completed);
  EXPECT_FLOAT_EQ(progress_events.back(), 1.0f);

  // Verify final file exists
  std::filesystem::path final_file = attachments_dir_ / (att_id + ".txt");
  ASSERT_TRUE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(final_file), file_data.size());
}

TEST_F(FileTransferResilienceTest, FailsGracefullyIfHashMismatch) {
  // Setup peer...
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other_cipher;
  peer->session_cipher->ComputeSharedSecret(other_cipher.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // 1. Receive Chat Message
  messaging::Attachment att;
  att.id = "bad_hash_file";
  att.type = messaging::AttachmentType::file;
  att.size_bytes = 10;
  att.hash = "deadbeef";  // Wrong hash
  att.is_downloaded = false;
  att.mime_type = "application/octet-stream";

  std::string payload = "payload";
  payload.insert(0, 1, (char)services::MessagingService::currentMessageVersion);
  auto encrypted = other_cipher.Encrypt(payload);

  service_->OnChatMessageReceived(peer1, encrypted, 9002, false, {att});

  // 2. Send Data
  std::string data = "0123456789";
  nlohmann::json chunk = {
      {"type", "file_chunk"},
      {"id", "bad_hash_file"},
      {"seq", 0},
      {"data", eillance::crypto::ToHex(StringToBytes(data))},
      {"eof", false}
  };
  service_->ProcessIncomingFileChunk(peer1, chunk);

  // 3. Send EOF
  bool completed = false;
  service_->SetAttachmentDownloadedCallback([&](auto, auto) { completed = true; });

  nlohmann::json eof = {{"type", "file_chunk"}, {"id", "bad_hash_file"}, {"seq", 1}, {"data", ""}, {"eof", true}};
  service_->ProcessIncomingFileChunk(peer1, eof);

  // 4. Verify
  EXPECT_FALSE(completed);  // Should not trigger callback on failure

  std::filesystem::path final_file = attachments_dir_ / ("bad_hash_file.dat");
  std::filesystem::path temp_file = attachments_dir_ / ("bad_hash_file.tmp");

  // Neither file should exist (temp is deleted on failure)
  EXPECT_FALSE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
}
