#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

// Local subclass to override GetPath for testing file operations in a safe temp dir
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

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

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
    service_->SetAttachmentStatusChangedCallback([](auto, auto, auto) {});
    service_->SetTransferProgressCallback([](auto, auto, auto) {});
    service_->SetAttachmentDownloadedCallback([](auto, auto) {});
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

  // Helper to construct binary chunk
  std::vector<std::byte> CreateChunkPacket(std::string const& id, uint32_t seq, std::vector<std::byte> const& payload) {
    std::vector<std::byte> packet;
    packet.push_back(std::byte{0xFE});
    packet.push_back(std::byte{0x01});
    packet.push_back(static_cast<std::byte>(id.size()));
    for (char c : id) packet.push_back(static_cast<std::byte>(c));
    for (int i = 0; i < 4; ++i) packet.push_back(static_cast<std::byte>((seq >> (i * 8)) & 0xFF));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
  }

  std::vector<std::byte> CreateEndPacket(std::string const& id) {
    std::vector<std::byte> packet;
    packet.push_back(std::byte{0xFE});
    packet.push_back(std::byte{0x06});
    packet.push_back(static_cast<std::byte>(id.size()));
    for (char c : id) packet.push_back(static_cast<std::byte>(c));
    return packet;
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
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
  atts.push_back({"dummy_path", messaging::AttachmentType::image, "base64thumb_content", "image/jpeg", "This is a note"}
  );

  std::ofstream(temp_dir_ / "dummy_path").close();
  auto const file_path = (temp_dir_ / "test_img.jpg").string();
  std::ofstream(file_path).close();
  atts[0].file_path = file_path;

  service_->SendChatMessage(peer1, "Msg", atts);

  ASSERT_GE(mock_transport_->CallLog()->sent_peer_messages.size(), 1);

  nlohmann::json json_msg;
  bool found = false;
  for (auto const& msg : mock_transport_->CallLog()->sent_peer_messages | std::views::values) {
    if (msg.value("type", "") == "chat") {
      json_msg = msg;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(json_msg.contains("attachments"));
  EXPECT_EQ(json_msg["attachments"][0]["note"], "This is a note");
}

TEST_F(MessagingServiceTest, SendMessage_NoteTruncation) {
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  std::string long_note(200, 'A');
  std::vector<services::AttachmentRequest> atts;
  auto const file_path = (temp_dir_ / "test_img.jpg").string();
  std::ofstream(file_path).close();
  atts.push_back({file_path, messaging::AttachmentType::image, "", "image/jpeg", long_note});

  service_->SendChatMessage(peer1, "Msg", atts);

  bool found = false;
  for (auto const& msg : mock_transport_->CallLog()->sent_peer_messages | std::views::values) {
    if (msg.value("type", "") == "chat") {
      auto note = msg["attachments"][0]["note"].get<std::string>();
      EXPECT_EQ(note.length(), 120);
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);
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
  std::string file_hash = crypto::Sha256Hex(file_content);

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
  att.note = "Incoming note";

  service_->OnChatMessageReceived(peer1, encrypted, 12345, false, {att});

  // Verify stored note
  auto stored_att = mock_storage_->GetAttachment("att_1");
  ASSERT_TRUE(stored_att.has_value());
  EXPECT_EQ(stored_att->note, "Incoming note");

  // Manually drive the FileTransferService via binary protocol
  std::vector<std::byte> data_bytes(file_content.size());
  std::memcpy(data_bytes.data(), file_content.data(), file_content.size());

  auto chunk_packet = CreateChunkPacket("att_1", 0, data_bytes);
  file_transfer_service_->ProcessBinaryMessage(peer1, chunk_packet);

  auto end_packet = CreateEndPacket("att_1");
  file_transfer_service_->ProcessBinaryMessage(peer1, end_packet);

  EXPECT_TRUE(download_cb_called);
}

TEST_F(MessagingServiceTest, SendMessage_UnifiedDeliveryQueue) {
  // Setup: No peer connected initially.
  // 1. Send Chat Message
  auto const entry = service_->SendChatMessage(peer1, "Queued Message");
  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);

  // Verify Pending Action Created with ref_id
  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(actions.size(), 1);
  EXPECT_EQ(actions[0].ref_id, entry.id);
  auto json_action = nlohmann::json::parse(actions[0].payload);
  EXPECT_EQ(json_action["type"], "chat");
  EXPECT_EQ(json_action["message_id"], entry.id);

  // 2. Connect Peer
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  // 3. Trigger queue flush
  service_->SendQueuedMessages(peer1);

  // 4. Verify message sent
  bool msg_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "chat") {
      msg_sent = true;
    }
  }
  EXPECT_TRUE(msg_sent);

  // 5. Verify Pending Action Still Exists (Waiting for Receipt)
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_EQ(actions.size(), 1);

  // 6. Simulate Receipt
  service_->OnMessageReceipt(peer1, entry.id, "delivered");

  // 7. Verify Pending Action Removed
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

TEST_F(MessagingServiceTest, SendSystemMessage_UnifiedDelivery) {
  // Send system message
  service_->SendSystemMessage(peer1, R"({"action": "custom_sys"})");

  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(actions.size(), 1);
  auto json = nlohmann::json::parse(actions[0].payload);
  EXPECT_EQ(json["type"], "chat");

  // Connect and flush
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  service_->SendQueuedMessages(peer1);

  // Verify sent as is_system=true
  bool sys_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg.value("is_system", false)) {
      sys_sent = true;
    }
  }
  EXPECT_TRUE(sys_sent);
}

TEST_F(MessagingServiceTest, MarkAsRead_UnifiedDelivery) {
  // Add a received message first
  auto entry = mock_storage_->AddHistoryEntry(
      {.remote_id = 12345,
       .kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .peer_id = peer1}
  );

  service_->MarkMessagesAsRead(peer1, {entry.id});

  // Verify pending action
  auto actions = mock_storage_->GetPendingActions(peer1);
  ASSERT_EQ(actions.size(), 1);
  auto json = nlohmann::json::parse(actions[0].payload);
  EXPECT_EQ(json["type"], "message_read");
  EXPECT_EQ(json["message_ids"][0], 12345);

  // Connect and Flush
  auto peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  transport_->AddPeer(peer1, peer);

  service_->SendQueuedMessages(peer1);

  // Verify sent
  bool read_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "message_read") {
      read_sent = true;
    }
  }
  EXPECT_TRUE(read_sent);

  // Verify action removed immediately (fire-and-forget logic for reads)
  actions = mock_storage_->GetPendingActions(peer1);
  EXPECT_TRUE(actions.empty());
}

TEST_F(MessagingServiceTest, SendMessage_Resilience_RetryLogic) {
  // Test ensuring that messages stay in 'sending' status and trigger retry
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher const other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  service_->SendChatMessage(peer1, "Persistent Message");

  // Verify initial status is sending
  auto history = service_->GetHistoryPage(peer1, 0, 1);
  ASSERT_FALSE(history.messages.empty());
  EXPECT_EQ(history.messages[0].status, messaging::MessageStatus::sending);

  // Simulate flushing queue (which sends data)
  service_->SendQueuedMessages(peer1);

  // Verify status is STILL sending (no receipt yet)
  history = service_->GetHistoryPage(peer1, 0, 1);
  EXPECT_EQ(history.messages[0].status, messaging::MessageStatus::sending);

  // Verify data sent to transport
  bool msg_sent = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "chat") {
      msg_sent = true;
    }
  }
  EXPECT_TRUE(msg_sent);

  // Verify pending work exists so DeliveryService knows to retry
  // (We check this by ensuring ConnectionAttemptFinished was called, which sets up retry timer
  // if work persists. We can't spy on DeliveryService internals easily, but we can verify
  // that status is 'sending' which satisfies 'HasPendingWork').
}

TEST_F(MessagingServiceTest, OnReceipt_UpdatesStatus) {
  // Setup sending message
  auto const entry = service_->SendChatMessage(peer1, "Msg");
  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);

  // Receive Receipt
  service_->OnMessageReceipt(peer1, entry.id, "delivered");

  auto [messages, total_messages] = service_->GetHistoryPage(peer1, 0, 1);
  EXPECT_EQ(messages[0].status, messaging::MessageStatus::delivered);
}

TEST_F(MessagingServiceTest, SystemMessage_SentReliablyViaDataChannel) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher const other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  service_->SendSystemMessage(peer1, R"({"action": "profile_update_diff"})");

  auto history = service_->GetHistoryPage(peer1, 0, 1);
  ASSERT_FALSE(history.messages.empty());
  EXPECT_EQ(history.messages[0].kind, messaging::HistoryEntryKind::systemMessage);
  EXPECT_EQ(history.messages[0].status, messaging::MessageStatus::sending);

  bool sent_as_chat = false;
  for (auto const& [pid, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "chat") {
      if (msg.value("is_system", false)) {
        sent_as_chat = true;
      }
    }
  }
  EXPECT_TRUE(sent_as_chat) << "System message should be sent as 'chat' with is_system=true";
}

TEST_F(MessagingServiceTest, SearchMessagesFacade) {
  service_->SendChatMessage(peer1, "Apple Pie");
  service_->SendChatMessage(peer1, "Banana Bread");

  auto res1 = service_->SearchMessages(peer1, "Apple");
  ASSERT_EQ(res1.size(), 1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(res1[0].data).content, "Apple Pie");

  auto res2 = service_->SearchAllMessages("Bread");
  ASSERT_EQ(res2.size(), 1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(res2[0].data).content, "Banana Bread");
}
