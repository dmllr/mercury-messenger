#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "eillance/client/client.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class FileTransferResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "ft_resilience_tests";
    std::filesystem::create_directories(temp_dir_);

    attachments_dir_ = temp_dir_ / "attachments";
    std::filesystem::create_directories(attachments_dir_);

    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>(temp_dir_.string() + "/db.sqlite");
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, media_transport);

    (void)client_->GenerateNewIdentity();

    client_->AddContact(peer1, std::nullopt);
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
    std::filesystem::remove_all(temp_dir_);
  }

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
    for (char c : id) {
      packet.push_back(static_cast<std::byte>(c));
    }

    for (int i = 0; i < 4; ++i) {
      packet.push_back(static_cast<std::byte>((seq >> (i * 8)) & 0xFF));
    }

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
  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(FileTransferResilienceTest, TransferProgressAndFileCreation) {
  eillance::crypto::SessionCipher other_cipher;

  std::vector<float> progress_events;
  auto sub1 = EventQueue::Instance().Subscribe<EventTransferProgress>([&](auto const& e) {
    progress_events.push_back(e.progress);
  });

  bool completed = false;
  auto sub2 = EventQueue::Instance().Subscribe<EventAttachmentDownloaded>([&](auto const& e) {
    if (e.peer_id == peer1 && e.attachment_id == "test_file_id") {
      completed = true;
    }
  });

  std::string const content = "Here is a large file";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = content;

  auto encrypted = other_cipher.Encrypt(payload_json.dump());

  std::string const file_data(20000, 'A');
  std::string const file_hash = crypto::Sha256Hex(file_data);
  std::string att_id = "test_file_id";

  messaging::Attachment att;
  att.id = att_id;
  att.type = messaging::AttachmentType::file;
  att.mime_type = "text/plain";
  att.size_bytes = file_data.size();
  att.hash = file_hash;
  att.is_downloaded = false;

  nlohmann::json payload_msg;
  payload_msg["type"] = "chat";
  payload_msg["message_id"] = 9001;
  payload_msg["payload"] = {{"c", content}, {"v", 2}};
  payload_msg["attachments"] = nlohmann::json::array(
      {{{"id", att_id},
        {"type", static_cast<int>(att.type)},
        {"mime", att.mime_type},
        {"size", att.size_bytes},
        {"hash", att.hash},
        {"is_downloaded", false}}}
  );

  storage::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  client_->Messaging()->ProjectMessage(entry, payload_msg, false);

  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  std::filesystem::path const temp_file = attachments_dir_ / (att_id + ".tmp");
  ASSERT_TRUE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(temp_file), file_data.size());

  constexpr auto chunk_size = services::FileTransferService::chunkSizeBytes;
  size_t const total_chunks = (file_data.size() + chunk_size - 1) / chunk_size;

  for (size_t seq = 0; seq < total_chunks; ++seq) {
    size_t const offset = seq * chunk_size;
    std::string const chunk_str = file_data.substr(offset, chunk_size);
    auto packet = CreateChunkPacket(att_id, seq, StringToBytes(chunk_str));
    EventQueue::Instance().Push(EventDataChannelMessage{peer1, packet});
  }

  auto end_packet = CreateEndPacket(att_id);
  EventQueue::Instance().Push(EventDataChannelMessage{peer1, end_packet});

  EventQueue::Instance().WaitFor([&] { return completed; }, std::chrono::seconds(2));

  EXPECT_TRUE(completed);
  EXPECT_FLOAT_EQ(progress_events.back(), 1.0f);

  EventQueue::Instance().Unsubscribe(sub1);
  EventQueue::Instance().Unsubscribe(sub2);

  std::filesystem::path const final_file = attachments_dir_ / (att_id + ".txt");
  ASSERT_TRUE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
  EXPECT_EQ(std::filesystem::file_size(final_file), file_data.size());
}

TEST_F(FileTransferResilienceTest, FailsGracefullyIfHashMismatch) {
  eillance::crypto::SessionCipher const other_cipher;

  messaging::Attachment att;
  att.id = "bad_hash_file";
  att.type = messaging::AttachmentType::file;
  att.size_bytes = 10;
  att.hash = "deadbeef";
  att.is_downloaded = false;
  att.mime_type = "application/octet-stream";

  std::string payload_content = "payload";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = payload_content;

  auto encrypted = other_cipher.Encrypt(payload_json.dump());

  client_->Messaging()->OnChatMessageReceived(peer1, encrypted, 9002, false, {att});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  std::string const data = "0123456789";
  auto chunk = CreateChunkPacket("bad_hash_file", 0, StringToBytes(data));
  EventQueue::Instance().Push(EventDataChannelMessage{peer1, chunk});

  bool completed = false;
  auto sub = EventQueue::Instance().Subscribe<EventAttachmentDownloaded>([&](auto const& e) {
    if (e.peer_id == peer1 && e.attachment_id == "bad_hash_file") {
      completed = true;
    }
  });

  auto end_packet = CreateEndPacket("bad_hash_file");
  EventQueue::Instance().Push(EventDataChannelMessage{peer1, end_packet});

  EventQueue::Instance().WaitFor([&] { return completed; }, std::chrono::milliseconds(500));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_FALSE(completed);

  std::filesystem::path const final_file = attachments_dir_ / ("bad_hash_file.dat");
  std::filesystem::path const temp_file = attachments_dir_ / ("bad_hash_file.tmp");

  EXPECT_FALSE(std::filesystem::exists(final_file));
  EXPECT_FALSE(std::filesystem::exists(temp_file));
}

TEST_F(FileTransferResilienceTest, StatusUpdateOrder_SmallFile) {
  std::vector<messaging::AttachmentStatus> status_sequence;
  auto sub = EventQueue::Instance().Subscribe<EventAttachmentStatusChanged>([&](auto const& e) {
    status_sequence.push_back(e.status);
  });

  std::filesystem::path const file_path = temp_dir_ / "small_order_test.dat";
  {
    std::ofstream ofs(file_path);
    ofs << "tiny";
  }

  std::vector<messaging::AttachmentRequest> atts;
  atts.push_back({file_path.string(), messaging::AttachmentType::file, "", "text/plain"});

  client_->SendChatMessage(peer1, "Sequence Test", atts);

  EventQueue::Instance().WaitFor([&] { return !status_sequence.empty(); }, std::chrono::seconds(2));
  ASSERT_GE(status_sequence.size(), 1);

  bool found_sequence = false;
  for (size_t i = 0; i < status_sequence.size(); ++i) {
    // Without network connection, it will stop at transferring
    if (status_sequence[i] == messaging::AttachmentStatus::transferring) {
      found_sequence = true;
      break;
    }
  }

  EventQueue::Instance().Unsubscribe(sub);

  EXPECT_TRUE(found_sequence) << "Status sequence should reach Transferring";
}
