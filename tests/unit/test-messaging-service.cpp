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
    temp_dir_ = std::filesystem::temp_directory_path() / "eillance_tests";

    // Ensure clean state from previous failed runs
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    std::filesystem::create_directories(temp_dir_, ec);

    try {
      temp_dir_ = std::filesystem::canonical(temp_dir_);
    } catch (...) {
      temp_dir_ = std::filesystem::absolute(temp_dir_);
    }

    // Pre-create attachments directory
    std::filesystem::create_directories(temp_dir_ / "attachments");

    auto storage_impl = std::make_unique<TestableMockStorage>(temp_dir_ / "test.db");
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    contact_manager_ = std::make_shared<identity::ContactManager>(storage_);
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
    file_transfer_service_.reset();
    storage_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
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
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::vector<services::AttachmentRequest> atts;
  atts.push_back({"dummy_path", messaging::AttachmentType::image, "base64thumb_content", "image/jpeg"});

  std::ofstream(temp_dir_ / "dummy_path").close();
  auto const file_path = (temp_dir_ / "test_img.jpg").string();
  std::ofstream(file_path).close();
  atts[0].file_path = file_path;

  service_->SendChatMessage(peer1, "Msg", atts);

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
  ASSERT_TRUE(found);
  ASSERT_TRUE(json_msg.contains("attachments"));
}

TEST_F(MessagingServiceTest, DownloadTriggersCallback) {
  bool download_cb_called = false;
  service_->SetAttachmentDownloadedCallback([&](auto pid, auto att_id) {
    if (pid == peer1 && att_id == "att_1") download_cb_called = true;
  });

  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other_cipher;
  peer->session_cipher->ComputeSharedSecret(other_cipher.GetPublicKey());
  other_cipher.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::string file_content = "Hello World Data";
  std::string file_hash = CalculateSha256(file_content);

  // Use new JSON format for encrypted payload
  std::string payload_content = "Here is a file";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = payload_content;

  auto encrypted = other_cipher.Encrypt(payload_json.dump());

  messaging::Attachment att;
  att.id = "att_1";
  att.type = messaging::AttachmentType::file;
  att.mime_type = "text/plain";
  att.size_bytes = file_content.size();
  att.hash = file_hash;
  att.is_downloaded = false;

  service_->OnChatMessageReceived(peer1, encrypted, 12345, false, {att});

  std::vector<std::byte> data_bytes(file_content.size());
  std::memcpy(data_bytes.data(), file_content.data(), file_content.size());
  std::string data_hex = eillance::crypto::ToHex(data_bytes);

  nlohmann::json chunk_msg = {{"type", "file_chunk"}, {"id", "att_1"}, {"seq", 0}, {"data", data_hex}, {"eof", false}};
  file_transfer_service_->ProcessMessage(peer1, chunk_msg);

  nlohmann::json eof_msg = {{"type", "file_chunk"}, {"id", "att_1"}, {"seq", 1}, {"data", ""}, {"eof", true}};
  file_transfer_service_->ProcessMessage(peer1, eof_msg);

  EXPECT_TRUE(download_cb_called);
}
