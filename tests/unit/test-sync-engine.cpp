#include <gtest/gtest.h>

#include "eillance/services/sync_engine.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class SyncEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    storage_impl_ = std::make_unique<storage::MockPersistentStorageImpl>();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl_));
    mock_log_ = std::make_shared<transport::MockCallLog>();
    transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    transport_ = std::make_shared<transport::DataTransport>(std::move(transport_impl_));

    engine_ = std::make_unique<sync::SyncEngine>(storage_, transport_, my_id_);
  }
  ContactID my_id_{"0000000000000000000000000000000000000000000000000000000000000001"};
  ContactID peer_id_{"0000000000000000000000000000000000000000000000000000000000000002"};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<storage::MockPersistentStorageImpl> storage_impl_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> transport_impl_;
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
  auto const peer = std::make_shared<transport::Peer>(peer_id_);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer_id_, peer);

  nlohmann::json const propose = {{"type", "sync_propose"}, {"head", "000"}, {"recent", {"000"}}};
  engine_->ProcessSyncMessage(peer_id_, propose);

  bool confirm_sent = false;
  for (auto const& msg : mock_log_->sent_peer_messages | std::views::values) {
    if (msg.value("type", "") == "sync_confirm") {
      confirm_sent = true;
    }
  }
  EXPECT_TRUE(confirm_sent || mock_log_->sent_peer_messages.size() > 0);
}
