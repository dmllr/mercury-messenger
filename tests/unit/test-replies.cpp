#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "eillance/crypto/crypto.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class RepliesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, media_transport);

    std::ignore = client_->GenerateNewIdentity();
    mock_storage_->UpdateContact({peer_id_, identity::ContactStatus::confirmed});
    client_->Contacts()->ReloadCache();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<Client> client_;
  ContactID const peer_id_ = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(RepliesTest, SendReplyMapsToWireId) {
  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .data = messaging::MessageEntry{"Incoming Message"},
       .peer_id = peer_id_}
  );

  client_->Messaging()->SendChatMessage(peer_id_, "Reply", {}, entry.id);

  auto ledger_entries = storage_->GetLedgerEntries(TopicID(peer_id_));
  ASSERT_GE(ledger_entries.size(), 1);
  auto json_payload = nlohmann::json::parse(ledger_entries.back().payload);
  EXPECT_EQ(json_payload["payload"].value("reply_to_id", 0ULL), entry.id);
}
