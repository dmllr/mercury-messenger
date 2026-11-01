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
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto mock_dt = std::make_unique<transport::MockDataTransport>();
    mock_dt_ = mock_dt.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(mock_dt));
    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);

    std::ignore = client_->GenerateNewIdentity();
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();
    client_->Start();
    mock_dt_->MarkVerified(peer1);
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
    std::filesystem::remove_all(temp_dir_);
  }

  bool IsBinaryPacket(std::vector<std::byte> const& data, std::uint8_t type, std::string const& id) {
    if (data.size() < 3 + id.size()) return false;
    if (data[0] != std::byte{0xFE}) return false;
    if (data[1] != std::byte{type}) return false;
    if (static_cast<uint8_t>(data[2]) != id.size()) return false;
    std::string packet_id(id.size(), '\0');
    std::memcpy(packet_id.data(), data.data() + 3, id.size());
    return packet_id == id;
  }

  std::vector<std::byte>
  CreateChunkPacket(std::string const& id, std::uint32_t seq, std::vector<std::byte> const& payload) {
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
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;
  transport::MockDataTransport* mock_dt_{};

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(FileTransferCancellationTest, CancelUpload) {
  std::filesystem::path const file_path = temp_dir_ / "large_file.dat";
  {
    std::ofstream ofs(file_path, std::ios::binary);
    std::string const data(16 * 16384 * 2, 'A');
    ofs << data;
  }

  std::vector<messaging::AttachmentRequest> attachments;
  attachments.push_back({file_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});

  auto record = client_->SendChatMessage(TopicID(peer1), "File", attachments);
  std::string const att_id = std::get<messaging::ChatMessageEntry>(record.data).message.attachments[0].id;
  EventQueue::Instance().WaitFor(
      [&]() {
        auto db_att = storage_->GetAttachment(att_id);
        return db_att && db_att->status == messaging::AttachmentStatus::transferring;
      },
      std::chrono::milliseconds(2000)
  );

  client_->CancelTransfer(TopicID(peer1), att_id, true);
  EventQueue::Instance().WaitFor(
      [&]() {
        auto db_att = storage_->GetAttachment(att_id);
        return db_att && db_att->status == messaging::AttachmentStatus::canceled;
      },
      std::chrono::milliseconds(2000)
  );

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

  messaging::ChatMessageEntry msg_data;
  msg_data.message.attachments.push_back(att);

  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::received,
       .data = msg_data,
       .topic_id = TopicID(peer1),
       .author_id = peer1}
  );

  std::vector<std::byte> const payload = {std::byte{0x00}, std::byte{0x11}, std::byte{0x22}};
  mock_dt_->MarkVerified(peer1);
  auto chunk = CreateChunkPacket("down_id", 0, payload);

  EventQueue::Instance().Push(EventDataChannelMessage{peer1, chunk});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  std::filesystem::path const temp_path = attachments_dir_ / "down_id.tmp";
  ASSERT_TRUE(std::filesystem::exists(temp_path));

  client_->CancelTransfer(TopicID(peer1), "down_id", false);
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  EXPECT_TRUE(std::filesystem::exists(temp_path));

  auto att_db = storage_->GetAttachment("down_id");
  ASSERT_TRUE(att_db.has_value());
  EXPECT_EQ(att_db->status, messaging::AttachmentStatus::paused);
}

TEST_F(FileTransferCancellationTest, ResumeSendsRequest) {
  messaging::Attachment att;
  att.id = "resume_id";
  att.is_downloaded = true;
  att.file_path = (temp_dir_ / "upload.dat").string();
  att.size_bytes = 100;
  att.hash = "abc";

  std::ofstream(att.file_path) << "data";

  messaging::ChatMessageEntry msg_data;
  msg_data.message.attachments.push_back(att);
  std::ignore = storage_->AddHistoryEntry({.data = msg_data, .topic_id = TopicID(peer1), .author_id = peer1});

  client_->ResumeTransfer(TopicID(peer1), "resume_id", false);
  EventQueue::Instance().WaitFor(
      [&] {
        auto db_att = storage_->GetAttachment("resume_id");
        return db_att && db_att->status == messaging::AttachmentStatus::transferring;
      },
      std::chrono::milliseconds(2000)
  );
}

// --- Security regression tests (audit H2): file transfer control messages require authorization ---

namespace {
auto ControlPacket(std::uint8_t type, std::string const& id) -> std::vector<std::byte> {
  std::vector<std::byte> packet{std::byte{0xFE}, std::byte{type}, static_cast<std::byte>(id.size())};
  for (char c : id) packet.push_back(static_cast<std::byte>(c));
  return packet;
}
}  // namespace

class FileTransferAuthorizationTest : public FileTransferCancellationTest {
 protected:
  void SetUp() override {
    FileTransferCancellationTest::SetUp();
    att_.id = "private_att";
    att_.is_downloaded = true;
    att_.file_path = (temp_dir_ / "private.dat").string();
    std::ofstream(att_.file_path) << "secret-content";
    att_.size_bytes = 14;
    att_.hash = crypto::CalculateFileHash(att_.file_path);
    att_.status = messaging::AttachmentStatus::done;

    messaging::ChatMessageEntry msg_data;
    msg_data.message.attachments.push_back(att_);
    std::ignore = storage_->AddHistoryEntry({.data = msg_data, .topic_id = TopicID(peer1), .author_id = peer1});
  }

  [[nodiscard]] auto ChunksSentTo(ContactID const& id) const -> int {
    int count = 0;
    for (auto const& [to, data] : mock_dt_->SentDataSnapshot()) {
      if (to == id && IsChunk(data)) ++count;
    }
    return count;
  }

  [[nodiscard]] static auto IsChunk(std::vector<std::byte> const& data) -> bool {
    return data.size() >= 2 && data[0] == std::byte{0xFE} && data[1] == std::byte{0x01};
  }

  messaging::Attachment att_;
};

TEST_F(FileTransferAuthorizationTest, UnverifiedStrangerCannotPullAttachment) {
  std::string mnemonic;
  ContactID const stranger(crypto::Identity::CreateNew(mnemonic)->GetPublicKey());

  EventQueue::Instance().Push(EventDataChannelMessage{stranger, ControlPacket(0x07, att_.id)});
  EventQueue::Instance().Push(EventDataChannelMessage{stranger, ControlPacket(0x05, att_.id)});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(300));

  EXPECT_EQ(ChunksSentTo(stranger), 0);
}

TEST_F(FileTransferAuthorizationTest, ConfirmedContactCannotPullOtherConversationsAttachment) {
  std::string mnemonic;
  ContactID const other(crypto::Identity::CreateNew(mnemonic)->GetPublicKey());
  mock_storage_->UpdateContact({other, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();
  mock_dt_->MarkVerified(other);
  mock_dt_->GetPeer(other)->data_channel_is_open = true;

  EventQueue::Instance().Push(EventDataChannelMessage{other, ControlPacket(0x07, att_.id)});
  EventQueue::Instance().Push(EventDataChannelMessage{other, ControlPacket(0x05, att_.id)});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(300));

  EXPECT_EQ(ChunksSentTo(other), 0);
}

TEST_F(FileTransferAuthorizationTest, ConfirmedContactCannotCancelOtherConversationsAttachment) {
  std::string mnemonic;
  ContactID const other(crypto::Identity::CreateNew(mnemonic)->GetPublicKey());
  mock_storage_->UpdateContact({other, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();
  mock_dt_->MarkVerified(other);

  EventQueue::Instance().Push(EventDataChannelMessage{other, ControlPacket(0x03, att_.id)});
  EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(200));

  auto const db_att = storage_->GetAttachment(att_.id);
  ASSERT_TRUE(db_att.has_value());
  EXPECT_NE(db_att->status, messaging::AttachmentStatus::peerCanceled);
}

TEST_F(FileTransferAuthorizationTest, ConversationPeerCanPullAttachment) {
  mock_dt_->MarkVerified(peer1);
  mock_dt_->GetPeer(peer1)->data_channel_is_open = true;

  EventQueue::Instance().Push(EventDataChannelMessage{peer1, ControlPacket(0x07, att_.id)});
  EventQueue::Instance().WaitFor([&] { return ChunksSentTo(peer1) > 0; }, std::chrono::milliseconds(2000));

  EXPECT_GT(ChunksSentTo(peer1), 0);
}
