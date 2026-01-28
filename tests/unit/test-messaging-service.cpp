#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

// Local subclass to override GetPath for testing file operations in a safe temp dir
class TestableMockStorage : public storage::MockPersistentStorageImpl {
  std::string db_path_;

 public:
  explicit TestableMockStorage(std::filesystem::path path)
      : db_path_(std::filesystem::absolute(path).string()) {}
  auto GetPath() const -> std::string override { return db_path_; }
};

class MessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup temporary directory for file tests
    // Use a shorter path to avoid MAX_PATH issues on Windows
    temp_dir_ = std::filesystem::temp_directory_path() / "eillance_tests";

    // Ensure clean state from previous failed runs
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    std::filesystem::create_directories(temp_dir_, ec);

    try {
      temp_dir_ = std::filesystem::canonical(temp_dir_);
    } catch (...) {
      // Fallback if canonical fails (e.g. symlinks not supported)
      temp_dir_ = std::filesystem::absolute(temp_dir_);
    }

    // Pre-create attachments directory
    std::filesystem::create_directories(temp_dir_ / "attachments");

    // Use the temp dir for the DB path so attachments (stored relative to DB) go there too
    auto storage_impl = std::make_unique<TestableMockStorage>(temp_dir_ / "test.db");
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

    file_transfer_service_ = std::make_shared<services::FileTransferService>(storage_, transport_);

    service_ = std::make_unique<services::MessagingService>(
        storage_,
        transport_,
        contact_manager_,
        file_transfer_service_,
        conn_handler
    );
    service_->SetChatMessageReceivedCallback([](auto, auto) {});
    service_->SetMessageStatusChangedCallback([](auto, auto, auto) {});
  }

  void TearDown() override {
    if (service_) {
      service_->StopDeliveryService();
    }
    service_.reset();
    file_transfer_service_.reset();  // Ensure files are closed before deletion
    // Release persistent storage BEFORE deleting files it might hold open (like SQLite DB)
    storage_.reset();
    // Best effort cleanup
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  std::string CreateTempFile(size_t size, std::string const& content_pattern = "a") const {
    static int counter = 0;
    std::string fname = "f" + std::to_string(++counter) + ".dat";
    std::filesystem::path path = temp_dir_ / fname;
    std::ofstream ofs(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (ofs.is_open()) {
      for (size_t i = 0; i < size; ++i) {
        ofs << content_pattern[i % content_pattern.size()];
      }
      ofs.flush();
      ofs.close();
    }
    return path.string();
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

  storage::MockPersistentStorageImpl* mock_storage_;
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<services::FileTransferService> file_transfer_service_;
  std::unique_ptr<services::MessagingService> service_;
  std::vector<ContactID> connection_requests_;
  std::filesystem::path temp_dir_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(MessagingServiceTest, SendMessage_ThumbInPayload) {
  // Setup connected peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<services::AttachmentRequest> atts;
  atts.push_back({"dummy_path", messaging::AttachmentType::image, "base64thumb_content", "image/jpeg"});

  // We need the file to exist for size calc
  std::ofstream(temp_dir_ / "dummy_path").close();
  // Adjust path relative to test exec or absolute if needed
  auto const file_path = (temp_dir_ / "test_img.jpg").string();
  std::ofstream(file_path).close();
  atts[0].file_path = file_path;

  service_->SendChatMessage(peer1, "Msg", atts);

  // Expect at least the chat message.
  // Note: An empty file attachment might trigger an immediate "EOF" file_chunk message as well,
  // so we assert >= 1 and look for the specific "chat" type message.
  ASSERT_GE(mock_transport_->log.sent_peer_messages.size(), 1);

  nlohmann::json json_msg;
  bool found = false;
  for (auto const& msg : mock_transport_->log.sent_peer_messages | std::views::values) {
    if (msg.value("type", "") == "chat") {
      json_msg = msg;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found) << "Chat message was not sent.";

  ASSERT_TRUE(json_msg.contains("attachments"));
  auto const& att_json = json_msg["attachments"][0];

  ASSERT_TRUE(att_json.contains("thumb"));
  ASSERT_EQ(att_json["mime"], "image/jpeg");
}

TEST_F(MessagingServiceTest, DownloadTriggersCallback) {
  bool download_cb_called = false;
  service_->SetAttachmentDownloadedCallback([&](auto pid, auto att_id) {
    if (pid == peer1 && att_id == "att_1") download_cb_called = true;
  });

  // Setup peer with crypto
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other_cipher;
  peer->session_cipher->ComputeSharedSecret(other_cipher.GetPublicKey());
  other_cipher.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // Prepare file data
  std::string file_content = "Hello World Data";
  std::string file_hash = CalculateSha256(file_content);

  // 1. Simulate receiving the message with attachment info
  std::string payload_content = "Here is a file";
  std::string versioned_payload;
  versioned_payload.push_back(static_cast<char>(services::MessagingService::currentMessageVersion));
  versioned_payload.append(payload_content);

  auto encrypted = other_cipher.Encrypt(versioned_payload);

  messaging::Attachment att;
  att.id = "att_1";
  att.type = messaging::AttachmentType::file;
  att.mime_type = "text/plain";
  att.size_bytes = file_content.size();
  att.hash = file_hash;
  att.is_downloaded = false;

  service_->OnChatMessageReceived(peer1, encrypted, 12345, false, {att});

  // 2. Simulate receiving chunks
  // Send data chunk
  std::vector<std::byte> data_bytes(file_content.size());
  std::memcpy(data_bytes.data(), file_content.data(), file_content.size());
  std::string data_hex = eillance::crypto::ToHex(data_bytes);

  nlohmann::json chunk_msg = {{"type", "file_chunk"}, {"id", "att_1"}, {"seq", 0}, {"data", data_hex}, {"eof", false}};
  file_transfer_service_->ProcessMessage(peer1, chunk_msg);

  // Send EOF chunk
  nlohmann::json eof_msg = {{"type", "file_chunk"}, {"id", "att_1"}, {"seq", 1}, {"data", ""}, {"eof", true}};
  file_transfer_service_->ProcessMessage(peer1, eof_msg);

  // 3. Verify
  EXPECT_TRUE(download_cb_called);
}
