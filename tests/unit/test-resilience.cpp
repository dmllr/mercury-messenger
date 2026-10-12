#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ResilienceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    client_ = std::make_shared<Client>(storage_, media_transport);

    (void)client_->Account()->GenerateNewIdentity();

    // Establish a confirmed contact context
    mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(ResilienceTest, SyncEnginePersistsUnsentReceipts) {
  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::delivered,
       .data = messaging::MessageEntry{"hello"},
       .peer_id = peer1}
  );

  client_->MarkMessagesAsRead(peer1, {entry.id});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  auto reqs = mock_storage_->GetSyncRequests();
  ASSERT_EQ(reqs.size(), 1);

  auto entries = storage_->GetLedgerEntries(TopicID(peer1));
  ASSERT_GE(entries.size(), 1);
  nlohmann::json const json = nlohmann::json::parse(entries.back().payload);
  EXPECT_EQ(json["type"], "message_read");
  EXPECT_EQ(json["message_ids"][0], entry.id);
}

TEST_F(ResilienceTest, ProfileUpdateSyncsOnConnect) {
  client_->UpdateMyProfile({.display_name = "New Name", .bio = "Bio", .photo_base64 = ""});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));

  bool sync_request_found = false;
  for (auto const& req : storage_->GetSyncRequests()) {
    if (req.target_peer_id == peer1) sync_request_found = true;
  }
  EXPECT_TRUE(sync_request_found);

  client_->Connect(peer1);
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return client_->GetPeerConnectionState(peer1) == transport::PeerConnectionState::connecting; },
      std::chrono::milliseconds(2000)
  );

  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        return client_->GetPeerConnectionState(peer1) == transport::PeerConnectionState::connecting;
      },
      std::chrono::milliseconds(2000)
  );
}

TEST_F(ResilienceTest, ReinstallRecovery_MessageReceipt) {
  // Simulate "wipe" by removing peer1 from contacts.
  client_->DeleteContact(peer1);
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(50));
  ASSERT_FALSE(client_->Contacts()->GetContact(peer1));

  // Connect implicitly triggered by messages from unknown sender (simulation)

  // Simulate receiving a chat message
  std::string plain = "Recovery Message";
  nlohmann::json payload_json;
  payload_json["type"] = "chat";
  payload_json["message_id"] = 100;
  payload_json["payload"] = {{"c", plain}, {"v", 2}};

  storage::LedgerEntry l_entry;
  l_entry.topic_id = TopicID(peer1);
  l_entry.author_id = peer1;
  client_->Messaging()->ProjectMessage(l_entry, payload_json, false);

  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto contact = client_->Contacts()->GetContact(peer1);
        return contact && contact->status == identity::ContactStatus::confirmed;
      },
      std::chrono::milliseconds(2000)
  );

  // Verify contact was auto-created and CONFIRMED
  auto contact = client_->Contacts()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  // Verify message stored
  auto [messages, total_messages] = client_->GetMessageHistoryPage(peer1, 0, 10);
  ASSERT_FALSE(messages.empty());
  auto const& data = std::get<messaging::MessageEntry>(messages[0].data);
  EXPECT_EQ(data.content, "Recovery Message");
}

TEST_F(ResilienceTest, ReinstallRecovery_ContactAdd) {
  ASSERT_TRUE(client_->Contacts()->IsConfirmed(peer1));

  nlohmann::json const req_msg = {{"type", "contact_add_request"}, {"profile", {{"display_name", "Reinstalled B"}}}};

  storage::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = storage::LedgerActionType::ContactState;
  entry.payload = req_msg.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& e : entries) {
          if (e.type == storage::LedgerActionType::ContactState &&
              e.payload.find("contact_confirm") != std::string::npos)
            return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool confirm_sent = false;
  for (auto const& e : storage_->GetLedgerEntries(TopicID(peer1))) {
    if (e.type == storage::LedgerActionType::ContactState && e.payload.find("contact_confirm") != std::string::npos) {
      confirm_sent = true;
    }
  }
  EXPECT_TRUE(confirm_sent) << "Should send confirmation via signaling";

  bool sync_request_exists = false;
  for (auto const& r : storage_->GetSyncRequests()) {
    if (r.target_peer_id == peer1) sync_request_exists = true;
  }
  EXPECT_TRUE(sync_request_exists) << "Should prepare profile sync request";
}

TEST_F(ResilienceTest, LedgerDeduplicatesIdenticalEntries) {
  storage::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.created_at = Timestamp{123};
  entry.type = storage::LedgerActionType::Message;
  entry.payload = "dummy payload";
  entry.hash = "111";

  storage_->ReplaceLedgerEntries(TopicID(peer1), std::string(64, '0'), {entry, entry});
  auto entries = storage_->GetLedgerEntries(TopicID(peer1));
  EXPECT_EQ(entries.size(), 1);
}
