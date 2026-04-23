#include <gtest/gtest.h>

#include "eillance/services/sync_engine.hpp"
#include "mock_call_log.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class SyncEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_impl_ = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl_));
    mock_log_ = std::make_shared<transport::MockCallLog>();
    transport_ = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());

    engine_ = std::make_unique<sync::SyncEngine>(storage_, transport_, my_id_);
  }
  ContactID my_id_{"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"};
  ContactID peer_id_{"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<storage::MockPersistentStorageImpl> storage_impl_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::unique_ptr<sync::SyncEngine> engine_;
};

TEST_F(SyncEngineTest, AppendActionCreatesLedgerEntryAndProposesSync) {
  nlohmann::json const payload = {{"key", "value"}};
  engine_->AppendAction(TopicID(peer_id_), storage::LedgerActionType::Message, payload);

  auto entries = storage_->GetLedgerEntries(TopicID(peer_id_));
  ASSERT_EQ(entries.size(), 1);
  EXPECT_EQ(entries[0].payload, payload.dump());

  auto const reqs = storage_->GetSyncRequests();
  ASSERT_EQ(reqs.size(), 1);
  EXPECT_EQ(reqs[0].target_peer_id, peer_id_);
}

TEST_F(SyncEngineTest, SyncProposeEmptyHead) {
  storage_->AddSyncRequest({TopicID(peer_id_), peer_id_, Timestamp::Now(), Timestamp{0}});

  nlohmann::json const propose = {
      {"type", "sync_propose"},
      {"head", std::string(64, '0')},
      {"recent", {std::string(64, '0')}}
  };
  engine_->ProcessSyncMessage(peer_id_, propose);

  // Verify that the sync_confirm effectively removes the sync request since heads matched
  auto const reqs = storage_->GetSyncRequests();
  EXPECT_TRUE(reqs.empty());
}
