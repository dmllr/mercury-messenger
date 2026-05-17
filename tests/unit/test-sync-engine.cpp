#include <gtest/gtest.h>

#include "eillance/services/sync_engine.hpp"
#include "mock_call_log.hpp"
#include "mock_data_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

struct PeerNode {
  ContactID id;
  std::shared_ptr<storage::PersistentStorage> storage;
  std::unique_ptr<storage::MockPersistentStorageImpl> storage_impl;
  std::shared_ptr<transport::DataTransport> transport;
  transport::MockDataTransport* transport_mock;
  std::unique_ptr<sync::SyncEngine> engine;
  std::shared_ptr<transport::Peer> peer_for_other;

  PeerNode(ContactID my_id, ContactID other_id)
      : id(my_id) {
    auto mock_storage = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage->UpdateContact({.peer_id = other_id, .status = identity::ContactStatus::confirmed});
    storage = std::make_shared<storage::PersistentStorage>(std::move(mock_storage));
    auto mock = std::make_unique<transport::MockDataTransport>();
    transport_mock = mock.get();
    transport = std::make_shared<transport::DataTransport>(std::move(mock));
    engine = std::make_unique<sync::SyncEngine>(storage, transport, id);

    peer_for_other = std::make_shared<transport::Peer>(other_id);
    peer_for_other->data_channel_is_open = true;
    transport_mock->AddPeer(other_id, peer_for_other);
  }

  void InitCipher(PeerNode& other) {
    auto pub = other.peer_for_other->session_cipher->GetPublicKey();
    peer_for_other->session_cipher->ComputeSharedSecret(pub);
  }
};

class SyncEngineTest : public ::testing::Test {
 protected:
  ContactID idA_{"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"};
  ContactID idB_{"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"};

  std::unique_ptr<PeerNode> A;
  std::unique_ptr<PeerNode> B;

  void SetUp() override {
    A = std::make_unique<PeerNode>(idA_, idB_);
    B = std::make_unique<PeerNode>(idB_, idA_);
    A->InitCipher(*B);
    B->InitCipher(*A);
  }

  void PumpMessages() {
    int guard = 0;
    while (guard++ < 100) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      
      bool activity = false;
      auto a_msgs = A->transport_mock->sent_jsons;
      A->transport_mock->sent_jsons.clear();
      for (auto& [peer, msg] : a_msgs) {
        if (peer == B->id) {
          B->engine->ProcessSyncMessage(A->id, msg);
          activity = true;
        }
      }
      auto b_msgs = B->transport_mock->sent_jsons;
      B->transport_mock->sent_jsons.clear();
      for (auto& [peer, msg] : b_msgs) {
        if (peer == A->id) {
          A->engine->ProcessSyncMessage(B->id, msg);
          activity = true;
        }
      }

      if (!activity) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (A->transport_mock->sent_jsons.empty() && B->transport_mock->sent_jsons.empty()) {
          break;
        }
      }
    }
  }
};

TEST_F(SyncEngineTest, SyncProposeEmptyHead) {
  A->storage->AddSyncRequest({TopicID(idB_), idB_, Timestamp::Now(), Timestamp{0}});

  nlohmann::json const propose = {
      {"type", "sync_propose"},
      {"topic", TopicID(idB_).ToString()},
      {"head", std::string(64, '0')}
  };
  A->engine->ProcessSyncMessage(idB_, propose);

  auto const reqs = A->storage->GetSyncRequests();
  EXPECT_TRUE(reqs.empty());
}

TEST_F(SyncEngineTest, IterativeSyncMissingEntries) {
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "1"}});
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "2"}});
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "3"}});

  PumpMessages();

  auto b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(b_entries.size(), 3);
  EXPECT_EQ(b_entries[0].hash, A->storage->GetLedgerEntries(TopicID(idB_))[0].hash);
  EXPECT_EQ(b_entries[2].hash, A->storage->GetLedgerEntries(TopicID(idB_))[2].hash);
}

TEST_F(SyncEngineTest, ConflictResolutionTakeBoth) {
  // Alice adds common LCA
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "LCA"}});
  PumpMessages();

  // Concurrent forks
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "A1"}});
  B->engine->AppendAction(TopicID(idA_), storage::LedgerActionType::Message, {{"msg", "B1"}});
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "A2"}});
  B->engine->AppendAction(TopicID(idA_), storage::LedgerActionType::Message, {{"msg", "B2"}});

  PumpMessages();

  auto a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  auto b_entries = B->storage->GetLedgerEntries(TopicID(idA_));

  // LCA + A1 + B1 + A2 + B2 = 5 entries
  ASSERT_EQ(a_entries.size(), 5);
  ASSERT_EQ(b_entries.size(), 5);

  // Both should have identical deterministic histories due to hash and order resolution
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(a_entries[i].hash, b_entries[i].hash);
    EXPECT_EQ(a_entries[i].payload, b_entries[i].payload);
  }
}

TEST_F(SyncEngineTest, PeerHasAllHistoryRemoved) {
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "1"}});
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "2"}});
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "3"}});
  PumpMessages();

  ASSERT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 3);

  // Bob removes history
  B->storage->DeleteLedgerEntriesAfter(TopicID(idA_), std::string(64, '0'));
  ASSERT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 0);

  // Alice appends new message
  A->engine->AppendAction(TopicID(idB_), storage::LedgerActionType::Message, {{"msg", "4"}});

  PumpMessages();

  // Bob should sync iteratively from 4 all the way to 000
  auto b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(b_entries.size(), 4);

  auto a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  EXPECT_EQ(b_entries[0].hash, a_entries[0].hash);
  EXPECT_EQ(b_entries[3].hash, a_entries[3].hash);
}
