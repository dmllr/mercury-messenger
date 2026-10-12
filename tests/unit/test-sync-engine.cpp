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

  void InitCipher(PeerNode const& other) const {
    auto const pub = other.peer_for_other->session_cipher->GetPublicKey();
    peer_for_other->session_cipher->ComputeSharedSecret(pub);
  }

  void InjectEntry(TopicID const& topic, std::string const& payload, uint64_t ts, ContactID const& author) const {
    storage::LedgerEntry entry;
    entry.topic_id = topic;
    entry.author_id = author;
    entry.created_at = Timestamp{ts};
    entry.type = storage::LedgerActionType::Message;
    entry.payload = nlohmann::json{{"msg", payload}}.dump();
    storage->AddLedgerEntry(entry);
  }
};

class SyncEngineTest : public ::testing::Test {
 protected:
  ContactID idA_{"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"};
  ContactID idB_{"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"};

  std::unique_ptr<PeerNode> A;
  std::unique_ptr<PeerNode> B;

  size_t total_transferred_A_to_B_ = 0;
  size_t total_transferred_B_to_A_ = 0;

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
          auto const type = msg.value("type", "");
          if (type == "sync_provide_forward_batch" || type == "sync_provide_fork") {
            total_transferred_A_to_B_ += msg["entries"].size();
          }
          B->engine->ProcessSyncMessage(A->id, msg);
          activity = true;
        }
      }
      auto b_msgs = B->transport_mock->sent_jsons;
      B->transport_mock->sent_jsons.clear();
      for (auto& [peer, msg] : b_msgs) {
        if (peer == A->id) {
          auto const type = msg.value("type", "");
          if (type == "sync_provide_forward_batch" || type == "sync_provide_fork") {
            total_transferred_B_to_A_ += msg["entries"].size();
          }
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

TEST_F(SyncEngineTest, SyncLargeAlmostIdentical_DifferenceRecent) {
  for (int i = 0; i < 100; ++i) {
    A->InjectEntry(TopicID(idB_), "common_" + std::to_string(i), 1000 + i, idA_);
    B->InjectEntry(TopicID(idA_), "common_" + std::to_string(i), 1000 + i, idA_);
  }
  A->InjectEntry(TopicID(idB_), "A_new_1", 2001, idA_);
  A->InjectEntry(TopicID(idB_), "A_new_2", 2002, idA_);

  B->InjectEntry(TopicID(idA_), "B_new_1", 2003, idB_);
  B->InjectEntry(TopicID(idA_), "B_new_2", 2004, idB_);

  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  EXPECT_EQ(A->storage->GetLedgerEntries(TopicID(idB_)).size(), 104);
  EXPECT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 104);
  EXPECT_LT(total_transferred_A_to_B_ + total_transferred_B_to_A_, 20);  // Sync shall not trigger full resync
}

TEST_F(SyncEngineTest, SyncLargeAlmostIdentical_DifferenceMiddle) {
  for (int i = 0; i < 50; ++i) {
    A->InjectEntry(TopicID(idB_), "common_" + std::to_string(i), 1000 + i, idA_);
    B->InjectEntry(TopicID(idA_), "common_" + std::to_string(i), 1000 + i, idA_);
  }
  for (int i = 50; i < 100; ++i) {
    A->InjectEntry(TopicID(idB_), "A_only_" + std::to_string(i), 1000 + i, idA_);
    B->InjectEntry(TopicID(idA_), "B_only_" + std::to_string(i), 1000 + i, idB_);
  }
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  EXPECT_EQ(A->storage->GetLedgerEntries(TopicID(idB_)).size(), 150);
  EXPECT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 150);
  EXPECT_GT(total_transferred_A_to_B_ + total_transferred_B_to_A_, 50);  // Half of messages are synched
}

TEST_F(SyncEngineTest, SyncLargeAlmostIdentical_DifferenceOldest) {
  A->InjectEntry(TopicID(idB_), "A_oldest", 999, idA_);
  B->InjectEntry(TopicID(idA_), "B_oldest", 999, idB_);
  for (int i = 0; i < 100; ++i) {
    A->InjectEntry(TopicID(idB_), "common_" + std::to_string(i), 1000 + i, idA_);
    B->InjectEntry(TopicID(idA_), "common_" + std::to_string(i), 1000 + i, idA_);
  }
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  EXPECT_EQ(A->storage->GetLedgerEntries(TopicID(idB_)).size(), 102);
  EXPECT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 102);
  EXPECT_GT(total_transferred_A_to_B_ + total_transferred_B_to_A_, 100);  // Full sync expected
}

TEST_F(SyncEngineTest, SyncIdenticalTimestamps_2Messages) {
  A->InjectEntry(TopicID(idB_), "msg_A", 1000, idA_);
  B->InjectEntry(TopicID(idA_), "msg_B", 1000, idB_);
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  auto const a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(a_entries.size(), 2);
  ASSERT_EQ(b_entries.size(), 2);
  EXPECT_EQ(a_entries[0].hash, b_entries[0].hash);
  EXPECT_EQ(a_entries[1].hash, b_entries[1].hash);
}

TEST_F(SyncEngineTest, SyncIdenticalTimestamps_10Messages) {
  for (int i = 0; i < 10; ++i) {
    if (i % 2 == 0) {
      A->InjectEntry(TopicID(idB_), "msg_A_" + std::to_string(i), 1000, idA_);
    } else {
      B->InjectEntry(TopicID(idA_), "msg_B_" + std::to_string(i), 1000, idB_);
    }
  }
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  auto const a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(a_entries.size(), 10);
  ASSERT_EQ(b_entries.size(), 10);
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(a_entries[i].hash, b_entries[i].hash);
  }
}

TEST_F(SyncEngineTest, SyncTotallyRandomHistories) {
  for (int i = 0; i < 50; ++i) {
    A->InjectEntry(TopicID(idB_), "rand_A_" + std::to_string(i), 1000 + (i * 13 % 100), idA_);
    B->InjectEntry(TopicID(idA_), "rand_B_" + std::to_string(i), 1000 + (i * 17 % 100), idB_);
  }
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  auto const a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(a_entries.size(), 100);
  ASSERT_EQ(b_entries.size(), 100);
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(a_entries[i].hash, b_entries[i].hash);
  }
}

TEST_F(SyncEngineTest, SyncSameHeadHashDifferInside) {
  A->InjectEntry(TopicID(idB_), "msg1", 1000, idA_);
  A->InjectEntry(TopicID(idB_), "msg2", 1001, idA_);
  A->InjectEntry(TopicID(idB_), "msg3", 1002, idA_);

  A->engine->ProposeSync(TopicID(idB_), idB_);
  auto const a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  ASSERT_EQ(a_entries.size(), 3);

  B->InjectEntry(TopicID(idA_), "msg1", 1000, idA_);

  storage::LedgerEntry forged_3;
  forged_3.topic_id = TopicID(idA_);
  forged_3.author_id = idA_;
  forged_3.created_at = Timestamp{1002};
  forged_3.type = storage::LedgerActionType::Message;
  forged_3.payload = nlohmann::json{{"msg", "msg3"}}.dump();
  forged_3.hash = a_entries[2].hash;
  forged_3.prev_hash = a_entries[2].prev_hash;
  B->storage->AddLedgerEntry(forged_3);

  B->engine->ProposeSync(TopicID(idA_), idA_);
  PumpMessages();

  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(b_entries.size(), 3);
  EXPECT_EQ(b_entries[0].payload, a_entries[0].payload);
  EXPECT_EQ(b_entries[1].payload, a_entries[1].payload);
  EXPECT_EQ(b_entries[2].payload, a_entries[2].payload);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(a_entries[i].hash, b_entries[i].hash);
  }
}
