#include <gtest/gtest.h>

#include "eillance/services/sync_engine.hpp"
#include "eillance/transport/libp2p_data_transport.hpp"
#include "mock_call_log.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class SyncEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_impl_ = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl_));
    mock_log_ = std::make_shared<transport::MockCallLog>();
    transport_ = std::make_shared<transport::DataTransport>(std::make_unique<transport::Libp2pDataTransport>());

    engine_ = std::make_unique<sync::SyncEngine>(storage_, transport_, my_id_);
  }
  ContactID my_id_{"0000000000000000000000000000000000000000000000000000000000000001"};
  ContactID peer_id_{"0000000000000000000000000000000000000000000000000000000000000002"};
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

  nlohmann::json const propose = {{"type", "sync_propose"}, {"head", "000"}, {"recent", {"000"}}};
  engine_->ProcessSyncMessage(peer_id_, propose);

  // Verify that the sync_confirm effectively removes the sync request since heads matched
  auto const reqs = storage_->GetSyncRequests();
  EXPECT_TRUE(reqs.empty());
}
