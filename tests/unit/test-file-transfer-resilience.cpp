#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
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

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    auto conn_handler = [](ContactID const&) {};

    service_ = std::make_unique<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        conn_handler
    );

    // Default dummy callbacks
    service_->SetChatMessageReceivedCallback([](auto, auto) {});
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
    service_->SetAttachmentDownloadedCallback([](auto, auto) {});
    service_->SetTransferProgressCallback([](auto, auto, auto) {});
  }

  void TearDown() override {
    service_->StopDeliveryService();
    service_.reset();
    file_transfer_service_.reset();  // Ensure files are closed before deletion
    std::filesystem::remove_all(temp_dir_);
  }

  static auto CalculateSha256(std::string const& data) -> std::string {
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
  static auto StringToBytes(std::string const& str) -> std::vector<std::byte> {
    std::vector<std::byte> bytes(str.size());
    std::ranges::transform(str, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
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

  std::vector<std::byte> CreateEndPacket(std::string const& id) {
    std::vector<std::byte> packet;
    packet.push_back(std::byte{0xFE});
    packet.push_back(std::byte{0x06});  // End Type
    packet.push_back(static_cast<std::byte>(id.size()));
    for (char c : id) packet.push_back(static_cast<std::byte>(c));
    return packet;
  }

  std::filesystem::path temp_dir_;
  std::filesystem::path attachments_dir_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
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
  service_->SetTransferProgressCallback([&](auto, auto const&, float p) -> auto { progress_events.push_back(p); });

  bool completed = false;
  service_->SetAttachmentDownloadedCallback([&](auto, auto const&) -> auto { completed = true; });

  // 1. Receive Chat Message with Attachment
  std::string const content = "Here is a large file";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = content;

  auto encrypted = other_cipher.Encrypt(payload_json.dump());

  std::string const file_data(20000, 'A');  // Larger than one chunk (16KB)
  std::string const file_hash = CalculateSha256(file_data);
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
  std::filesystem::path const temp_file = attachments_dir_ / (att_id + ".tmp");
  ASSERT_TRUE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(temp_file), file_data.size());

  // 2. Process Chunks using Binary Protocol
  constexpr auto chunk_size = services::FileTransferService::kChunkSizeBytes;
  size_t const total_chunks = (file_data.size() + chunk_size - 1) / chunk_size;

  for (size_t seq = 0; seq < total_chunks; ++seq) {
    size_t const offset = seq * chunk_size;
    std::string const chunk_str = file_data.substr(offset, chunk_size);
    auto packet = CreateChunkPacket(att_id, seq, StringToBytes(chunk_str));
    file_transfer_service_->ProcessBinaryMessage(peer1, packet);
  }

  // 3. End
  auto end_packet = CreateEndPacket(att_id);
  file_transfer_service_->ProcessBinaryMessage(peer1, end_packet);

  // Verify completion
  EXPECT_TRUE(completed);
  EXPECT_FLOAT_EQ(progress_events.back(), 1.0f);

  // Verify final file exists
  std::filesystem::path const final_file = attachments_dir_ / (att_id + ".txt");
  ASSERT_TRUE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(final_file), file_data.size());
}

TEST_F(FileTransferResilienceTest, FailsGracefullyIfHashMismatch) {
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

  std::string payload_content = "payload";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = payload_content;

  auto encrypted = other_cipher.Encrypt(payload_json.dump());

  service_->OnChatMessageReceived(peer1, encrypted, 9002, false, {att});

  // 2. Send Data
  std::string const data = "0123456789";
  auto chunk = CreateChunkPacket("bad_hash_file", 0, StringToBytes(data));
  file_transfer_service_->ProcessBinaryMessage(peer1, chunk);

  // 3. Send End
  bool completed = false;
  service_->SetAttachmentDownloadedCallback([&](auto, auto) { completed = true; });

  auto end_packet = CreateEndPacket("bad_hash_file");
  file_transfer_service_->ProcessBinaryMessage(peer1, end_packet);

  // 4. Verify
  EXPECT_FALSE(completed);  // Should not trigger callback on failure

  std::filesystem::path const final_file = attachments_dir_ / ("bad_hash_file.dat");
  std::filesystem::path const temp_file = attachments_dir_ / ("bad_hash_file.tmp");

  // Neither file should exist (temp is deleted on failure)
  EXPECT_FALSE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
}

TEST_F(FileTransferResilienceTest, StatusUpdateOrder_SmallFile) {
  // Setup peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<messaging::AttachmentStatus> status_sequence;
  service_->SetAttachmentStatusChangedCallback([&](auto, auto, auto status) { status_sequence.push_back(status); });

  // Create small file
  std::filesystem::path file_path = temp_dir_ / "small_order_test.dat";
  {
    std::ofstream ofs(file_path);
    ofs << "tiny";
  }

  std::vector<services::AttachmentRequest> atts;
  atts.push_back({file_path.string(), messaging::AttachmentType::file, "", "text/plain"});

  // Send
  service_->SendChatMessage(peer1, "Sequence Test", atts);

  // Expect sequence: Transferring -> Done
  // If bug exists: Done -> Transferring
  ASSERT_GE(status_sequence.size(), 2);

  bool found_sequence = false;
  for (size_t i = 0; i < status_sequence.size() - 1; ++i) {
    if (status_sequence[i] == messaging::AttachmentStatus::transferring &&
        status_sequence[i + 1] == messaging::AttachmentStatus::done) {
      found_sequence = true;
      break;
    }
  }

  EXPECT_TRUE(found_sequence) << "Status sequence should be Transferring -> Done";
  if (!status_sequence.empty()) {
    EXPECT_NE(status_sequence.back(), messaging::AttachmentStatus::transferring)
        << "Should not end in Transferring state for completed upload";
  }
}
