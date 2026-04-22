#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include "eillance/client/client.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class TestableMockStorage : public storage::MockPersistentStorageImpl {
  std::string db_path_;

 public:
  explicit TestableMockStorage(std::filesystem::path path)
      : db_path_(std::filesystem::absolute(path).string()) {
  }
  auto GetPath() const -> std::string override {
    return db_path_;
  }
};

class MessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "eillance_tests";

    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    std::filesystem::create_directories(temp_dir_, ec);

    try {
      temp_dir_ = std::filesystem::canonical(temp_dir_);
    } catch (...) {
      temp_dir_ = std::filesystem::absolute(temp_dir_);
    }

    std::filesystem::create_directories(temp_dir_ / "attachments");

    auto storage_impl = std::make_unique<TestableMockStorage>(temp_dir_ / "test.db");
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, media_transport);

    std::ignore = client_->GenerateNewIdentity();
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();

    client_->Start();

    conn_sub_id_ = EventQueue::Instance().Subscribe<EventConnectionRequested>([this](auto const& e) {
      connection_requests_.push_back(e.peer_id);
    });
  }

  void TearDown() override {
    EventQueue::Instance().Unsubscribe(conn_sub_id_);
    client_->Terminate();
    client_.reset();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
  }

  std::vector<std::byte> CreateChunkPacket(std::string const& id, uint32_t seq, std::vector<std::byte> const& payload) {
    std::vector<std::byte> packet;
    packet.push_back(std::byte{0xFE});
    packet.push_back(std::byte{0x01});
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
    packet.push_back(std::byte{0x06});
    packet.push_back(static_cast<std::byte>(id.size()));
    for (char c : id) {
      packet.push_back(static_cast<std::byte>(c));
    }
    return packet;
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;
  std::vector<ContactID> connection_requests_;
  std::filesystem::path temp_dir_;
  uint64_t conn_sub_id_ = 0;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(MessagingServiceTest, SendMessage_ThumbInPayload) {
  std::vector<messaging::AttachmentRequest> atts;
  atts.push_back({"dummy_path", messaging::AttachmentType::image, "base64thumb_content", "image/jpeg", "This is a note"}
  );

  std::ofstream(temp_dir_ / "dummy_path").close();
  auto const file_path = (temp_dir_ / "test_img.jpg").string();
  std::ofstream(file_path).close();
  atts[0].file_path = file_path;

  client_->SendChatMessage(peer1, "Msg", atts);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = storage_->GetLedgerEntries(TopicID(peer1));
        return !entries.empty();
      },
      std::chrono::milliseconds(2000)
  );

  auto entries = storage_->GetLedgerEntries(TopicID(peer1));
  ASSERT_GE(entries.size(), 1);
  nlohmann::json json_msg = nlohmann::json::parse(entries.back().payload);
  ASSERT_TRUE(json_msg.contains("attachments"));
  EXPECT_EQ(json_msg["attachments"][0]["note"], "This is a note");
}

TEST_F(MessagingServiceTest, DownloadTriggersCallback) {
  client_->Connect(peer1);
  eillance::EventQueue::Instance().Push(EventDataChannelOpen{peer1});
  eillance::crypto::SessionCipher dummy_cipher;
  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", dummy_cipher.GetPublicKey()},
      {"is_reply", true},
      {"sync_version", 2}
  };
  std::string key_str = key_msg.dump();
  std::vector<std::byte> key_bytes(key_str.size());
  std::transform(key_str.begin(), key_str.end(), key_bytes.begin(), [](char c){ return static_cast<std::byte>(c); });
  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, key_bytes});
  eillance::EventQueue::Instance().WaitFor([]{ return false; }, std::chrono::milliseconds(50));

  bool download_cb_called = false;
  auto sub = EventQueue::Instance().Subscribe<EventAttachmentDownloaded>([&](auto const& e) {
    if (e.peer_id == peer1 && e.attachment_id == "att_1") {
      download_cb_called = true;
    }
  });

  std::string file_content = "Hello World Data";
  std::string const file_hash = crypto::Sha256Hex(file_content);

  std::string payload_content = "Here is a file";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = payload_content;

  messaging::Attachment att;
  att.id = "att_1";
  att.type = messaging::AttachmentType::file;
  att.mime_type = "text/plain";
  att.size_bytes = file_content.size();
  att.hash = file_hash;
  att.is_downloaded = false;
  att.note = "Incoming note";

  client_->Messaging()->OnChatMessageReceived(peer1, payload_json, 12345, false, {att});

  auto stored_att = mock_storage_->GetAttachment("att_1");
  ASSERT_TRUE(stored_att.has_value());
  EXPECT_EQ(stored_att->note, "Incoming note");

  std::vector<std::byte> data_bytes(file_content.size());
  std::memcpy(data_bytes.data(), file_content.data(), file_content.size());

  auto chunk_packet = CreateChunkPacket("att_1", 0, data_bytes);
  EventQueue::Instance().Push(EventDataChannelMessage{peer1, chunk_packet});

  auto end_packet = CreateEndPacket("att_1");
  EventQueue::Instance().Push(EventDataChannelMessage{peer1, end_packet});

  EventQueue::Instance().WaitFor([&] { return download_cb_called; }, std::chrono::seconds(1));

  EventQueue::Instance().Unsubscribe(sub);
  EXPECT_TRUE(download_cb_called);
}

TEST_F(MessagingServiceTest, SendMessage_AppendsToLedger) {
  auto const entry = client_->SendChatMessage(peer1, "Ledger Message");
  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);

  auto entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].type, storage::LedgerActionType::Message);
  EXPECT_TRUE(entries[0].payload.find("Ledger Message") != std::string::npos);

  eillance::EventQueue::Instance().Push(eillance::EventSendQueuedMessages{peer1});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = storage_->GetLedgerEntries(TopicID(peer1));
        return !entries.empty();
      },
      std::chrono::milliseconds(2000)
  );

  bool msg_recorded = false;
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& e : entries) {
          if (e.payload.find("Ledger Message") != std::string::npos) {
            msg_recorded = true;
            return true;
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );
  EXPECT_TRUE(msg_recorded);
}

TEST_F(MessagingServiceTest, MarkAsRead_AppendsToLedger) {
  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .peer_id = peer1}
  );

  client_->MarkMessagesAsRead(peer1, {entry.id});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  auto entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
  ASSERT_GE(entries.size(), 1);

  eillance::EventQueue::Instance().Push(eillance::EventSendQueuedMessages{peer1});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto db_entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
        return !db_entries.empty();
      },
      std::chrono::milliseconds(2000)
  );

  bool read_recorded = false;
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto db_entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& e : db_entries) {
          if (e.payload.find("message_read") != std::string::npos) {
            read_recorded = true;
            return true;
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );
  EXPECT_TRUE(read_recorded);
}
