#include <algorithm>
#include <thread>

#include <gtest/gtest.h>

#include "eillance/messaging/ledger_signature.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/service_manager.hpp"
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

  PeerNode(ContactID my_id, ContactID other_id, sync::SyncEngine::Signer signer = {})
      : id(my_id) {
    auto mock_storage = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage->UpdateContact({.peer_id = other_id, .status = identity::ContactStatus::confirmed});
    storage = std::make_shared<storage::PersistentStorage>(std::move(mock_storage));
    auto mock = std::make_unique<transport::MockDataTransport>();
    transport_mock = mock.get();
    transport = std::make_shared<transport::DataTransport>(std::move(mock));
    engine = std::make_unique<sync::SyncEngine>(storage, transport, id, std::move(signer));

    peer_for_other = std::make_shared<transport::Peer>(other_id);
    peer_for_other->data_channel_is_open = true;
    peer_for_other->crcp_verified = true;
    transport_mock->AddPeer(other_id, peer_for_other);
  }

  void InitCipher(PeerNode const& other) const {
    auto const pub = other.peer_for_other->session_cipher->GetPublicKey();
    peer_for_other->session_cipher->ComputeSharedSecret(pub);
  }

  void InjectEntry(TopicID const& topic, std::string const& payload, std::uint64_t ts, ContactID const& author) const {
    messaging::LedgerEntry entry;
    entry.topic_id = topic;
    entry.author_id = author;
    entry.created_at = Timestamp{ts};
    entry.type = messaging::LedgerActionType::Message;
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
      std::this_thread::sleep_for(std::chrono::milliseconds(2));

      bool activity = false;
      auto a_msgs = A->transport_mock->sent_jsons;
      A->transport_mock->sent_jsons.clear();
      for (auto& [peer, msg] : a_msgs) {
        if (peer == B->id) {
          auto const type = msg.value("type", "");
          if (type == "sync_batch" || type == "sync_batch_reply") {
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
          if (type == "sync_batch" || type == "sync_batch_reply") {
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
      {"type", "sync_start"},
      {"topic", TopicID(idB_).ToString()},
      {"head", messaging::LedgerHash().ToString()},
      {"size", 0}
  };
  A->engine->ProcessSyncMessage(idB_, propose);

  // Need to process the mock event queue loop due to async finish wrapper
  PumpMessages();

  auto const reqs = A->storage->GetSyncRequests();
  EXPECT_TRUE(reqs.empty());
}

TEST_F(SyncEngineTest, IterativeSyncMissingEntries) {
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "1"}});
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "2"}});
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "3"}});

  PumpMessages();

  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(b_entries.size(), 3);
  EXPECT_EQ(b_entries[0].hash, A->storage->GetLedgerEntries(TopicID(idB_))[0].hash);
  EXPECT_EQ(b_entries[2].hash, A->storage->GetLedgerEntries(TopicID(idB_))[2].hash);
}

TEST_F(SyncEngineTest, ConflictResolutionTakeBoth) {
  // Alice adds common LCA
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "LCA"}});
  PumpMessages();

  // Concurrent forks
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "A1"}});
  B->engine->AppendAction(TopicID(idA_), messaging::LedgerActionType::Message, {{"msg", "B1"}});
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "A2"}});
  B->engine->AppendAction(TopicID(idA_), messaging::LedgerActionType::Message, {{"msg", "B2"}});

  PumpMessages();

  auto const a_entries = A->storage->GetLedgerEntries(TopicID(idB_));
  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));

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
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "1"}});
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "2"}});
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "3"}});
  PumpMessages();

  ASSERT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 3);

  // Bob removes history
  B->storage->DeleteLedgerEntriesAfter(TopicID(idA_), messaging::LedgerHash());
  ASSERT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 0);

  // Alice appends new message
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "4"}});

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

  messaging::LedgerEntry forged_3;
  forged_3.topic_id = TopicID(idA_);
  forged_3.author_id = idA_;
  forged_3.created_at = Timestamp{1002};
  forged_3.type = messaging::LedgerActionType::Message;
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

TEST_F(SyncEngineTest, SyncHistoryLargerThanMaxBatchSize) {
  for (int i = 0; i < 1200; ++i) {
    A->InjectEntry(TopicID(idB_), "batch_msg_" + std::to_string(i), 1000 + i, idA_);
  }

  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  auto b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  EXPECT_EQ(b_entries.size(), 1200);

  EXPECT_GE(total_transferred_A_to_B_, 1200);
  EXPECT_EQ(total_transferred_B_to_A_, 0);
}

// Without signatures (legacy entries) our own messages cannot be restored from a contact: the contact could
// otherwise forge entries in our name. See SignedSyncEngineTest.RestoreDeletedContactHistoryTriggersSyncedEvents
// for the signed (current) behaviour.
TEST_F(SyncEngineTest, RestoreDeletedContactHistoryRejectsUnsignedOwnEntries) {
  A->engine->AppendAction(
      TopicID(idB_),
      messaging::LedgerActionType::Message,
      {{"type", "chat"}, {"message_id", 1}, {"payload", {{"c", "Hello from A"}}}}
  );
  B->engine->AppendAction(
      TopicID(idA_),
      messaging::LedgerActionType::Message,
      {{"type", "chat"}, {"message_id", 2}, {"payload", {{"c", "Hello from B"}}}}
  );

  PumpMessages();

  // B deletes contact A
  B->storage->DeleteLedgerEntriesAfter(TopicID(idA_), messaging::LedgerHash());

  int synced_events_received = 0;
  auto sub_id = EventQueue::Instance().Subscribe<EventLedgerAppended>([&](auto const& e) {
    if (e.is_synced && e.entry.author_id == idB_) {
      synced_events_received++;
    }
  });

  B->engine->ProposeSync(TopicID(idA_), idA_);
  PumpMessages();

  EventQueue::Instance().Unsubscribe(sub_id);

  // B's own unsigned message relayed by A is not accepted
  EXPECT_EQ(synced_events_received, 0);
  EXPECT_EQ(B->storage->GetLedgerEntries(TopicID(idA_)).size(), 1U);
}

TEST_F(SyncEngineTest, SyncValidateAndRepairChain) {
  A->InjectEntry(TopicID(idB_), "msg1", 1000, idA_);
  A->InjectEntry(TopicID(idB_), "msg2", 1001, idA_);

  // Corrupt hash of msg2
  auto entries = A->storage->GetLedgerEntries(TopicID(idB_));
  entries[1].hash = messaging::LedgerHash(std::string(64, 'f'));
  A->storage->ReplaceLedgerEntries(TopicID(idB_), messaging::LedgerHash(), entries);

  // Proposing sync triggers ValidateAndRepairChain which fixes the corrupted hash
  A->engine->ProposeSync(TopicID(idB_), idB_);
  PumpMessages();

  auto repaired = A->storage->GetLedgerEntries(TopicID(idB_));
  EXPECT_NE(repaired[1].hash.ToString(), std::string(64, 'f'));

  auto b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  EXPECT_EQ(b_entries.size(), 2);
  EXPECT_EQ(repaired[0].hash, b_entries[0].hash);
  EXPECT_EQ(repaired[1].hash, b_entries[1].hash);
}

TEST_F(SyncEngineTest, SyncKeepsReadStatusInsteadOfDowngradingToDelivered) {
  services::ServiceManager sm;
  sm.storage = A->storage;
  sm.data_transport = A->transport;
  sm.contact_manager = std::make_shared<identity::ContactManager>(A->storage);
  sm.group_manager = std::make_shared<identity::GroupManager>(A->storage);
  sm.channel_manager = std::make_shared<identity::ChannelManager>(A->storage);

  sm.account = std::make_shared<services::AccountService>(&sm);
  std::ignore = sm.account->GenerateNewIdentity();
  sm.file_transfer = std::make_shared<services::FileTransferService>(&sm);

  auto messaging_service = std::make_shared<services::MessagingService>(&sm);
  sm.messaging = messaging_service;

  messaging::LedgerEntry chat_entry;
  chat_entry.topic_id = TopicID(idB_);
  chat_entry.author_id = idA_;
  chat_entry.created_at = Timestamp{1000};
  chat_entry.type = messaging::LedgerActionType::Message;

  nlohmann::json const chat_json = {{"type", "chat"}, {"message_id", 100}, {"payload", {{"c", "Hello"}}}};
  messaging_service->ProjectMessage(chat_entry, chat_json, true, false);

  messaging::LedgerEntry read_entry;
  read_entry.topic_id = TopicID(idB_);
  read_entry.author_id = idB_;
  read_entry.created_at = Timestamp{1001};
  read_entry.type = messaging::LedgerActionType::Message;
  nlohmann::json const read_json = {{"type", "message_read"}, {"message_ids", {100}}};
  messaging_service->ProjectMessage(read_entry, read_json, false, false);

  messaging::LedgerEntry delivered_entry;
  delivered_entry.topic_id = TopicID(idB_);
  delivered_entry.author_id = idB_;
  delivered_entry.created_at = Timestamp{1002};
  delivered_entry.type = messaging::LedgerActionType::Message;
  nlohmann::json const delivered_json = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_ids", {100}}};
  messaging_service->ProjectMessage(delivered_entry, delivered_json, false, false);

  auto entry = A->storage->GetHistoryEntry(TopicID(idB_), 100);
  if (entry) {
    EXPECT_EQ(entry->status, messaging::MessageStatus::read);
  }
}

// --- Security regression tests (audit H1): peers may only sync topics they participate in ---

namespace {
auto CountEntriesSentForTopic(transport::MockDataTransport const* mock, TopicID const& topic) -> size_t {
  size_t count = 0;
  for (auto const& [to, msg] : mock->sent_jsons) {
    if (msg.value("topic", "") == topic.ToString() && msg.contains("entries")) {
      count += msg["entries"].size();
    }
  }
  return count;
}
}  // namespace

TEST_F(SyncEngineTest, PeerCannotSyncForeignOneToOneTopic) {
  ContactID const idC("aa4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e99");
  A->InjectEntry(TopicID(idC), "private conversation with C", 1000, idC);

  nlohmann::json const checkpoints = {
      {"type", "sync_checkpoints"}, {"topic", TopicID(idC).ToString()}, {"checkpoints", {""}}
  };
  A->engine->ProcessSyncMessage(idB_, checkpoints);
  nlohmann::json const batch = {
      {"type", "sync_batch"},
      {"topic", TopicID(idC).ToString()},
      {"lca", ""},
      {"batch_index", 0},
      {"total_entries", 0},
      {"entries", nlohmann::json::array()}
  };
  A->engine->ProcessSyncMessage(idB_, batch);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(CountEntriesSentForTopic(A->transport_mock, TopicID(idC)), 0U);
}

TEST_F(SyncEngineTest, PeerCannotSyncGroupItIsNotMemberOf) {
  identity::Group g;
  g.id = GroupID("bb4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e11");
  g.owner_id = idA_;
  g.members = {{idA_, 0}};
  A->storage->AddGroup(g);
  A->InjectEntry(TopicID(g.id), "group secret", 1000, idA_);

  nlohmann::json const checkpoints = {
      {"type", "sync_checkpoints"}, {"topic", TopicID(g.id).ToString()}, {"checkpoints", {""}}
  };
  A->engine->ProcessSyncMessage(idB_, checkpoints);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(CountEntriesSentForTopic(A->transport_mock, TopicID(g.id)), 0U);
}

TEST_F(SyncEngineTest, GroupMemberCanSyncGroup) {
  identity::Group g;
  g.id = GroupID("cc4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e22");
  g.owner_id = idA_;
  g.members = {{idA_, 0}, {idB_, 0}};
  A->storage->AddGroup(g);
  A->InjectEntry(TopicID(g.id), "group message", 1000, idA_);

  nlohmann::json const checkpoints = {
      {"type", "sync_checkpoints"}, {"topic", TopicID(g.id).ToString()}, {"checkpoints", {""}}
  };
  A->engine->ProcessSyncMessage(idB_, checkpoints);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  EXPECT_EQ(CountEntriesSentForTopic(A->transport_mock, TopicID(g.id)), 1U);
}

// --- Security regression tests (audit C4): ledger entries are authenticated by their author ---

namespace {
auto MakeSigner(crypto::Identity const* ident) -> sync::SyncEngine::Signer {
  return [ident](std::span<std::byte const> bytes) { return ident->Sign(bytes); };
}

auto SignWith(crypto::Identity const& ident, std::string const& canonical, messaging::LedgerEntry const& e)
    -> std::vector<std::byte> {
  auto const bytes = messaging::LedgerSigningBytes(canonical, e);
  return ident.Sign(std::span(reinterpret_cast<std::byte const*>(bytes.data()), bytes.size()));
}
}  // namespace

// Real identities for both nodes, with signing enabled.
class SignedSyncEngineTest : public SyncEngineTest {
 protected:
  void SetUp() override {
    std::string mnemonic;
    alice_ = crypto::Identity::CreateNew(mnemonic);
    bob_ = crypto::Identity::CreateNew(mnemonic);
    idA_ = ContactID(alice_->GetPublicKey());
    idB_ = ContactID(bob_->GetPublicKey());
    A = std::make_unique<PeerNode>(idA_, idB_, MakeSigner(alice_.get()));
    B = std::make_unique<PeerNode>(idB_, idA_, MakeSigner(bob_.get()));
    A->InitCipher(*B);
    B->InitCipher(*A);
  }

  std::unique_ptr<crypto::Identity> alice_;
  std::unique_ptr<crypto::Identity> bob_;
};

TEST_F(SignedSyncEngineTest, SignedEntriesSyncAndVerifyAcrossPerspectives) {
  A->engine->AppendAction(TopicID(idB_), messaging::LedgerActionType::Message, {{"msg", "from A"}});
  B->engine->AppendAction(TopicID(idA_), messaging::LedgerActionType::Message, {{"msg", "from B"}});
  PumpMessages();

  auto const b_entries = B->storage->GetLedgerEntries(TopicID(idA_));
  ASSERT_EQ(b_entries.size(), 2U);
  for (auto const& e : b_entries) {
    EXPECT_FALSE(e.signature.empty());
  }
  EXPECT_EQ(A->storage->GetLedgerEntries(TopicID(idB_)).size(), 2U);
}

TEST_F(SignedSyncEngineTest, RestoreDeletedContactHistoryTriggersSyncedEvents) {
  A->engine->AppendAction(
      TopicID(idB_),
      messaging::LedgerActionType::Message,
      {{"type", "chat"}, {"message_id", 1}, {"payload", {{"c", "Hello from A"}}}}
  );
  B->engine->AppendAction(
      TopicID(idA_),
      messaging::LedgerActionType::Message,
      {{"type", "chat"}, {"message_id", 2}, {"payload", {{"c", "Hello from B"}}}}
  );
  PumpMessages();

  B->storage->DeleteLedgerEntriesAfter(TopicID(idA_), messaging::LedgerHash());

  int synced_events_received = 0;
  auto sub_id = EventQueue::Instance().Subscribe<EventLedgerAppended>([&](auto const& e) {
    if (e.is_synced && e.entry.author_id == idB_) {
      synced_events_received++;
    }
  });

  B->engine->ProposeSync(TopicID(idA_), idA_);
  PumpMessages();
  EventQueue::Instance().Unsubscribe(sub_id);

  // B's own signed message is restored from A
  EXPECT_EQ(synced_events_received, 1);
}

// "Me" is Bob; Carol is a confirmed contact relaying entries in a group shared with Alice.
class LedgerRelayTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string mnemonic;
    alice_ = crypto::Identity::CreateNew(mnemonic);
    bob_ = crypto::Identity::CreateNew(mnemonic);
    carol_ = crypto::Identity::CreateNew(mnemonic);
    alice_id_ = ContactID(alice_->GetPublicKey());
    bob_id_ = ContactID(bob_->GetPublicKey());
    carol_id_ = ContactID(carol_->GetPublicKey());

    me_ = std::make_unique<PeerNode>(bob_id_, carol_id_, MakeSigner(bob_.get()));
    me_->peer_for_other->session_cipher->ComputeSharedSecret(carol_cipher_.GetPublicKey());
    carol_cipher_.ComputeSharedSecret(me_->peer_for_other->session_cipher->GetPublicKey());

    identity::Group g;
    g.id = gid_;
    g.owner_id = alice_id_;
    g.members = {{alice_id_, 1}, {bob_id_, 0}, {carol_id_, 0}};
    me_->storage->AddGroup(g);
  }

  auto Entry(ContactID const& author, TopicID const& topic, std::string const& text) -> messaging::LedgerEntry {
    messaging::LedgerEntry e;
    e.topic_id = topic;
    e.author_id = author;
    e.created_at = Timestamp{++ts_};
    e.type = messaging::LedgerActionType::Message;
    e.payload = nlohmann::json{{"msg", text}}.dump();
    return e;
  }

  // Carol sends one entry to us via sync_batch for `wire_topic`.
  void DeliverFromCarol(std::string const& wire_topic, messaging::LedgerEntry const& e) {
    nlohmann::json const entry_json = {
        {"author", e.author_id.ToString()},
        {"created", e.created_at.value},
        {"type", static_cast<int>(e.type)},
        {"payload", crypto::ToHex(carol_cipher_.Encrypt(e.payload))},
        {"prev_hash", ""},
        {"hash", ""},
        {"sig", crypto::ToHex(e.signature)}
    };
    nlohmann::json const msg = {
        {"type", "sync_batch"},
        {"topic", wire_topic},
        {"lca", ""},
        {"batch_index", 0},
        {"total_entries", 1},
        {"entries", nlohmann::json::array({entry_json})}
    };
    me_->engine->ProcessSyncMessage(carol_id_, msg);
  }

  [[nodiscard]] auto Stored(TopicID const& topic, std::string const& text) const -> bool {
    auto const payload = nlohmann::json{{"msg", text}}.dump();
    return std::ranges::any_of(me_->storage->GetLedgerEntries(topic), [&](auto const& e) {
      return e.payload == payload;
    });
  }

  std::unique_ptr<crypto::Identity> alice_, bob_, carol_;
  ContactID alice_id_, bob_id_, carol_id_;
  std::unique_ptr<PeerNode> me_;
  crypto::SessionCipher carol_cipher_;
  GroupID const gid_{"ee4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e44"};
  std::uint64_t ts_ = 1000;
};

TEST_F(LedgerRelayTest, SignedThirdPartyEntryIsAccepted) {
  auto e = Entry(alice_id_, TopicID(gid_), "alice says hi");
  e.signature = SignWith(*alice_, messaging::CanonicalGroupTopic(TopicID(gid_)), e);
  DeliverFromCarol(TopicID(gid_).ToString(), e);
  EXPECT_TRUE(Stored(TopicID(gid_), "alice says hi"));
}

TEST_F(LedgerRelayTest, UnsignedThirdPartyEntryIsRejected) {
  DeliverFromCarol(TopicID(gid_).ToString(), Entry(alice_id_, TopicID(gid_), "forged alice"));
  EXPECT_FALSE(Stored(TopicID(gid_), "forged alice"));
}

TEST_F(LedgerRelayTest, ForgedEntryAuthoredByMeIsRejected) {
  DeliverFromCarol(TopicID(gid_).ToString(), Entry(bob_id_, TopicID(gid_), "forged me unsigned"));
  EXPECT_FALSE(Stored(TopicID(gid_), "forged me unsigned"));

  auto e = Entry(bob_id_, TopicID(gid_), "forged me carol-signed");
  e.signature = SignWith(*carol_, messaging::CanonicalGroupTopic(TopicID(gid_)), e);
  DeliverFromCarol(TopicID(gid_).ToString(), e);
  EXPECT_FALSE(Stored(TopicID(gid_), "forged me carol-signed"));
}

TEST_F(LedgerRelayTest, TamperedPayloadIsRejected) {
  auto e = Entry(alice_id_, TopicID(gid_), "original");
  e.signature = SignWith(*alice_, messaging::CanonicalGroupTopic(TopicID(gid_)), e);
  e.payload = nlohmann::json{{"msg", "tampered"}}.dump();
  DeliverFromCarol(TopicID(gid_).ToString(), e);
  EXPECT_FALSE(Stored(TopicID(gid_), "tampered"));
}

TEST_F(LedgerRelayTest, SignatureForAnotherTopicIsRejected) {
  GroupID const other("ff4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e55");
  auto e = Entry(alice_id_, TopicID(gid_), "moved");
  e.signature = SignWith(*alice_, messaging::CanonicalGroupTopic(TopicID(other)), e);
  DeliverFromCarol(TopicID(gid_).ToString(), e);
  EXPECT_FALSE(Stored(TopicID(gid_), "moved"));
}

TEST_F(LedgerRelayTest, UnsignedLegacyEntryFromItsAuthorIsAccepted) {
  DeliverFromCarol(TopicID(gid_).ToString(), Entry(carol_id_, TopicID(gid_), "legacy carol"));
  EXPECT_TRUE(Stored(TopicID(gid_), "legacy carol"));
}

TEST_F(LedgerRelayTest, OneToOneSignatureVerifiesOnReceiver) {
  // Carol stores our conversation under TopicID(bob); she signs the canonical pair {carol, bob}.
  auto e = Entry(carol_id_, TopicID(bob_id_), "carol 1:1");
  e.signature = SignWith(*carol_, messaging::CanonicalPairTopic(carol_id_, TopicID(bob_id_)), e);
  DeliverFromCarol(TopicID(bob_id_).ToString(), e);  // wire topic = our ID, mapped to TopicID(carol) locally
  EXPECT_TRUE(Stored(TopicID(carol_id_), "carol 1:1"));
}
