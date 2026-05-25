#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/data_types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/timestamp.hpp"

namespace eillance::sync {

class SyncEngine {
  using Json = nlohmann::json;

  static constexpr size_t maxBatchSize = 25;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  ContactID my_id_;

  std::atomic<bool> isRunning_{false};
  DispatchQueue dispatchQueue_{"SyncEngine", 1};
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  struct SyncSession {
    ContactID peer_id;
    bool active = false;
    messaging::LedgerHash current_lca;

    int zero_progress_loops = 0;
    messaging::LedgerHash last_head;

    std::vector<messaging::LedgerEntry> postponed_events;

    std::vector<messaging::LedgerEntry> my_entries_to_send;
    size_t peer_total_entries = 0;
    size_t current_batch_index = 0;
  };

  std::unordered_map<ContactID, SyncSession> sessions_;
  std::mutex sessions_mutex_;
  std::mutex ledger_mutex_;

  static auto ComputeSHA256(std::string const& data) -> messaging::LedgerHash {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length_of_hash = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
    EVP_DigestUpdate(context, data.c_str(), data.length());
    EVP_DigestFinal_ex(context, hash, &length_of_hash);
    EVP_MD_CTX_free(context);
    std::span<std::byte const> const bytes(reinterpret_cast<std::byte const*>(hash), length_of_hash);
    return messaging::LedgerHash(bytes);
  }

  auto ValidateAndRepairChain(TopicID const& topic_id) const -> std::pair<messaging::LedgerHash, size_t> {
    auto entries = storage_->GetLedgerEntries(topic_id);
    if (entries.empty()) {
      return {messaging::LedgerHash(), 0};
    }

    size_t const original_size = entries.size();

    std::ranges::sort(entries, [](auto const& a, auto const& b) -> bool {
      if (a.created_at.value != b.created_at.value) {
        return a.created_at.value < b.created_at.value;
      }
      if (a.author_id != b.author_id) {
        return a.author_id < b.author_id;
      }
      if (a.type != b.type) {
        return a.type < b.type;
      }
      return a.payload < b.payload;
    });

    auto unique_range = std::ranges::unique(entries, [](auto const& a, auto const& b) -> bool {
      return a.created_at.value == b.created_at.value && a.author_id.ToString() == b.author_id.ToString() &&
             a.type == b.type && a.payload == b.payload;
    });
    entries.erase(unique_range.begin(), unique_range.end());

    bool chain_broken = (entries.size() != original_size);
    auto current_hash = messaging::LedgerHash();

    for (auto& entry : entries) {
      if (entry.prev_hash != current_hash) {
        chain_broken = true;
        entry.prev_hash = current_hash;
      }
      auto const data_to_hash = entry.prev_hash.ToString() + entry.author_id.ToString() +
                                std::to_string(entry.created_at.value) + std::to_string(static_cast<int>(entry.type)) +
                                entry.payload;
      if (auto const computed_hash = ComputeSHA256(data_to_hash); entry.hash != computed_hash) {
        chain_broken = true;
        entry.hash = computed_hash;
      }
      current_hash = entry.hash;
    }

    if (chain_broken) {
      storage_->ReplaceLedgerEntries(topic_id, messaging::LedgerHash(), entries);
    }

    return {current_hash, entries.size()};
  }

  auto BuildCheckpoints(messaging::LedgerHash const& head, TopicID const& topic_id) -> std::vector<std::string> {
    std::vector<messaging::LedgerHash> all_hashes;
    messaging::LedgerHash curr = head;
    while (!curr.IsEmpty()) {
      all_hashes.push_back(curr);
      auto const entry = storage_->GetLedgerEntryByHash(topic_id, curr);
      if (!entry) {
        break;
      }
      curr = entry->prev_hash;
    }
    all_hashes.emplace_back();

    std::vector<std::string> chk;
    for (size_t i = 0; i < all_hashes.size(); ++i) {
      if (i < 50 || i % maxBatchSize == 0) {
        chk.push_back(all_hashes[i].ToString());
      }
    }
    if (chk.back() != all_hashes.back().ToString()) {
      chk.push_back(all_hashes.back().ToString());
    }
    return chk;
  }

  void FinishSync(ContactID const& peer_id, TopicID const& topic_id) {
    std::vector<messaging::LedgerEntry> events_to_fire;
    {
      std::lock_guard<std::mutex> const lock(sessions_mutex_);
      auto& session = sessions_[peer_id];
      session.active = false;
      events_to_fire = std::move(session.postponed_events);
      session.postponed_events.clear();
      session.zero_progress_loops = 0;
      session.last_head = messaging::LedgerHash();
    }

    storage_->RemoveSyncRequest(topic_id, peer_id);

    for (auto const& e : events_to_fire) {
      EventQueue::Instance().Push(EventLedgerAppended{e});
    }

    EventQueue::Instance().Push(EventSyncStateChanged{peer_id, false, 1.0f});
    EventQueue::Instance().Push(EventDataReloaded{});
    LGI << "[Sync] Sync finished with " << peer_id.ToString();
  }

  auto ParseEntries(ContactID const& peer_id, TopicID const& topic_id, Json const& msg_entries) const
      -> std::vector<messaging::LedgerEntry> {
    std::vector<messaging::LedgerEntry> parsed;
    auto const peer = transport_->GetPeer(peer_id);
    if (!peer || !peer->session_cipher->IsReady()) {
      return parsed;
    }

    for (auto const& e_json : msg_entries) {
      messaging::LedgerEntry e;
      e.topic_id = topic_id;
      e.author_id = ContactID(e_json["author"].get<std::string>());
      e.created_at = Timestamp{e_json["created"].get<uint64_t>()};
      e.type = static_cast<messaging::LedgerActionType>(e_json["type"].get<int>());
      e.prev_hash = messaging::LedgerHash(e_json.value("prev_hash", ""));
      e.hash = messaging::LedgerHash(e_json.value("hash", ""));

      if (auto const encrypted_bytes = crypto::FromHex(e_json["payload"].get<std::string>());
          !encrypted_bytes.empty()) {
        auto const decrypted = peer->session_cipher->Decrypt(encrypted_bytes);
        if (!decrypted.empty()) {
          e.payload = std::string(decrypted.data(), decrypted.size());
          parsed.push_back(e);
        } else {
          LGE << "[Sync] Decryption failed for entry " << e.hash.ToString();
        }
      }
    }
    return parsed;
  }

 public:
  SyncEngine(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> transport,
      ContactID const& my_id
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        my_id_(my_id) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSendQueuedMessages>([this](auto const& e) -> void {
      dispatchQueue_.Dispatch([this, peer_id = e.peer_id]() -> void {
        if (auto const peer = transport_->GetPeer(peer_id);
            !peer || !peer->session_cipher->IsReady() || !peer->crcp_verified) {
          return;
        }
        auto const reqs = storage_->GetSyncRequests();
        for (auto const& req : reqs) {
          if (req.target_peer_id == peer_id) {
            ProposeSync(req.topic_id, peer_id);
          }
        }
      });
    }));

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataPeerConnectionStateChanged>([this](auto const& e) -> void {
          if (e.state == transport::PeerConnectionState::disconnected ||
              e.state == transport::PeerConnectionState::failed || e.state == transport::PeerConnectionState::closed) {
            std::lock_guard<std::mutex> const lock(sessions_mutex_);
            if (sessions_.contains(e.peer_id)) {
              sessions_[e.peer_id].active = false;
            }
          }
        })
    );
  }

  ~SyncEngine() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
    Stop();
  }

  void SetMyId(ContactID const& my_id) {
    my_id_ = my_id;
  }

  void Start() {
    if (isRunning_.exchange(true)) {
      return;
    }
    dispatchQueue_.Dispatch([this]() -> void { ProcessPendingSyncs(); });
  }

  void Stop() {
    isRunning_ = false;
    dispatchQueue_.RemovePending();
  }

  void AppendAction(TopicID const& topic_id, messaging::LedgerActionType type, Json const& payload) {
    messaging::LedgerEntry entry;
    entry.topic_id = topic_id;
    entry.author_id = my_id_;
    entry.created_at = Timestamp::Now();
    entry.type = type;
    entry.payload = payload.dump();

    ContactID const target_peer(topic_id.value.Span());

    std::unique_lock<std::mutex> lock(ledger_mutex_);
    auto latest = storage_->GetLatestLedgerEntry(topic_id);

    if (latest && entry.created_at.value <= latest->created_at.value) {
      entry.created_at.value = latest->created_at.value + 1;
    }

    entry.prev_hash = latest ? latest->hash : messaging::LedgerHash();

    std::string const data_to_hash = entry.prev_hash.ToString() + entry.author_id.ToString() +
                                     std::to_string(entry.created_at.value) +
                                     std::to_string(static_cast<int>(entry.type)) + entry.payload;
    entry.hash = ComputeSHA256(data_to_hash);

    storage_->AddLedgerEntry(entry);
    lock.unlock();

    bool is_syncing = false;
    {
      std::lock_guard<std::mutex> const slock(sessions_mutex_);
      if (sessions_[target_peer].active) {
        is_syncing = true;
        sessions_[target_peer].postponed_events.push_back(entry);
      }
    }

    if (!is_syncing) {
      EventQueue::Instance().Push(EventLedgerAppended{entry});
    }

    storage_->AddSyncRequest({topic_id, target_peer, Timestamp::Now(), Timestamp{0}});

    auto contact = storage_->GetContact(target_peer);
    if (!contact || contact->status != identity::ContactStatus::confirmed) {
      LGE << "Sync error/rejection: Contact " << target_peer.ToString() << " is not confirmed.";
      return;
    }

    if (auto peer = transport_->GetPeer(target_peer); peer && peer->data_channel_is_open && peer->crcp_verified) {
      ProposeSync(topic_id, target_peer);
    } else {
      if (type != messaging::LedgerActionType::ProfileUpdate) {
        EventQueue::Instance().Push(EventConnectionRequested{target_peer});
      }
    }
  }

  void VerifySyncState(TopicID const& topic_id, ContactID const& target_peer) {
    messaging::LedgerHash head;
    size_t size = 0;
    {
      std::unique_lock<std::mutex> const lock(ledger_mutex_);
      auto const [fst, snd] = ValidateAndRepairChain(topic_id);
      head = fst;
      size = snd;
    }
    LGI << "[Sync] Verifying sync state with " << target_peer.ToString() << " for topic " << topic_id.ToString()
        << ". Head: " << head.ToString() << " Size: " << size;
    Json const msg =
        {{"type", "sync_start"}, {"topic", topic_id.ToString()}, {"head", head.ToString()}, {"size", size}};
    transport_->SendJson(target_peer, msg);
  }

  void ProposeSync(TopicID const& topic_id, ContactID const& target_peer) {
    {
      std::unique_lock<std::mutex> const lock(ledger_mutex_);
      std::ignore = ValidateAndRepairChain(topic_id);
    }

    {
      std::lock_guard<std::mutex> const lock(sessions_mutex_);
      auto& session = sessions_[target_peer];
      if (session.active) {
        return;
      }
      session.peer_id = target_peer;
      session.active = true;
      session.current_lca = messaging::LedgerHash();
      session.zero_progress_loops = 0;
      session.last_head = messaging::LedgerHash();
      session.postponed_events.clear();
    }

    EventQueue::Instance().Push(EventSyncStateChanged{target_peer, true, 0.0f});

    dispatchQueue_.Dispatch([this, topic_id, target_peer]() -> void { VerifySyncState(topic_id, target_peer); });
  }

  void ProcessSyncMessage(ContactID const& peer_id, Json const& msg) {
    try {
      std::string const type = msg.value("type", "");

      TopicID const topic_id(peer_id);

      if (type == "sync_start") {
        LGD << "[Sync] Received sync_start from " << peer_id.ToString() << " with head " << msg.value("head", "");
        messaging::LedgerHash const peer_head(msg.value("head", ""));
        size_t const peer_size = msg.value("size", static_cast<size_t>(0));

        std::unique_lock<std::mutex> lock(ledger_mutex_);
        auto const [my_head, my_size] = ValidateAndRepairChain(topic_id);

        if (peer_head == my_head && peer_size == my_size) {
          LGI << "[Sync] Heads and sizes match! Sync complete with " << peer_id.ToString();
          Json const reply = {{"type", "sync_done"}, {"topic", topic_id.ToString()}};
          transport_->SendJson(peer_id, reply);
          lock.unlock();
          dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { FinishSync(peer_id, topic_id); });
          return;
        }

        if (peer_head == my_head && peer_size != my_size) {
          LGE << "[Sync] CRITICAL: Same head but different size! peer_size=" << peer_size << " my_size=" << my_size;
        }

        auto checkpoints = BuildCheckpoints(my_head, topic_id);
        lock.unlock();

        {
          std::lock_guard<std::mutex> const slock(sessions_mutex_);
          auto& session = sessions_[peer_id];
          session.active = true;
          session.last_head = peer_head;
        }

        EventQueue::Instance().Push(EventSyncStateChanged{peer_id, true, 0.0f});

        Json const req = {{"type", "sync_checkpoints"}, {"topic", topic_id.ToString()}, {"checkpoints", checkpoints}};
        transport_->SendJson(peer_id, req);

      } else if (type == "sync_checkpoints") {
        LGD << "[Sync] Received sync_checkpoints from " << peer_id.ToString();
        auto checkpoints = msg.value("checkpoints", std::vector<std::string>{});
        messaging::LedgerHash lca;

        std::unique_lock<std::mutex> lock(ledger_mutex_);
        for (auto const& cp_str : checkpoints) {
          messaging::LedgerHash const cp(cp_str);
          if (cp.IsEmpty() || storage_->HasLedgerEntry(topic_id, cp)) {
            lca = cp;
            break;
          }
        }
        auto my_entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
        lock.unlock();

        LGI << "[Sync] Found LCA " << lca.ToString() << " for peer " << peer_id.ToString() << " with "
            << my_entries.size() << " entries to send.";

        std::ranges::sort(my_entries, [](auto const& a, auto const& b) -> bool {
          if (a.created_at.value != b.created_at.value) {
            return a.created_at.value < b.created_at.value;
          }
          if (a.author_id != b.author_id) {
            return a.author_id < b.author_id;
          }
          if (a.type != b.type) {
            return a.type < b.type;
          }
          return a.payload < b.payload;
        });

        {
          std::lock_guard<std::mutex> const slock(sessions_mutex_);
          auto& session = sessions_[peer_id];
          session.active = true;

          if (session.current_lca == lca) {
            session.zero_progress_loops++;
            if (session.zero_progress_loops > 3) {
              LGE << "[Sync] LCA did not change! Stuck in loop with " << peer_id.ToString() << ". Aborting sync.";
              dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { FinishSync(peer_id, topic_id); });
              return;
            }
          } else {
            session.zero_progress_loops = 0;
          }
          session.current_lca = lca;
          session.my_entries_to_send = my_entries;
          session.current_batch_index = 0;
          session.peer_total_entries = 0;
        }

        size_t const end = std::min<size_t>(maxBatchSize, my_entries.size());
        std::vector<messaging::LedgerEntry> const batch(my_entries.begin(), my_entries.begin() + end);

        Json entries_json = Json::array();
        if (auto peer = transport_->GetPeer(peer_id); peer && peer->session_cipher->IsReady()) {
          for (auto const& e : batch) {
            auto encrypted = peer->session_cipher->Encrypt(e.payload);
            if (!encrypted.empty()) {
              entries_json.push_back(
                  {{"author", e.author_id.ToString()},
                   {"created", e.created_at.value},
                   {"type", static_cast<int>(e.type)},
                   {"payload", crypto::ToHex(encrypted)},
                   {"prev_hash", e.prev_hash.ToString()},
                   {"hash", e.hash.ToString()}}
              );
            }
          }
        }

        Json const req = {
            {"type", "sync_batch"},
            {"topic", topic_id.ToString()},
            {"lca", lca.ToString()},
            {"batch_index", 0},
            {"total_entries", my_entries.size()},
            {"entries", entries_json}
        };
        transport_->SendJson(peer_id, req);

      } else if (type == "sync_batch") {
        messaging::LedgerHash const lca(msg.value("lca", ""));
        size_t const batch_index = msg.value("batch_index", 0ULL);
        size_t const peer_total_entries = msg.value("total_entries", 0ULL);

        LGI << "[Sync] Received sync_batch from " << peer_id.ToString() << " batch_index: " << batch_index
            << " containing " << msg["entries"].size() << " entries.";

        auto peer_entries = ParseEntries(peer_id, topic_id, msg["entries"]);
        std::vector<messaging::LedgerEntry> my_batch;
        size_t my_total_entries = 0;

        {
          std::lock_guard<std::mutex> const slock(sessions_mutex_);
          auto& session = sessions_[peer_id];
          session.active = true;

          if (batch_index == 0) {
            std::unique_lock<std::mutex> const db_lock(ledger_mutex_);
            auto my_entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
            std::ranges::sort(my_entries, [](auto const& a, auto const& b) -> bool {
              if (a.created_at.value != b.created_at.value) {
                return a.created_at.value < b.created_at.value;
              }
              if (a.author_id != b.author_id) {
                return a.author_id < b.author_id;
              }
              if (a.type != b.type) {
                return a.type < b.type;
              }
              return a.payload < b.payload;
            });
            session.my_entries_to_send = my_entries;
          }
          session.current_batch_index = batch_index;
          session.peer_total_entries = peer_total_entries;
          my_total_entries = session.my_entries_to_send.size();

          size_t const start_idx = batch_index * maxBatchSize;
          if (start_idx < my_total_entries) {
            size_t const end_idx = std::min<size_t>(start_idx + maxBatchSize, my_total_entries);
            my_batch.assign(
                session.my_entries_to_send.begin() + start_idx,
                session.my_entries_to_send.begin() + end_idx
            );
          }
        }

        std::vector<messaging::LedgerEntry> actually_new;
        if (!peer_entries.empty()) {
          std::unique_lock<std::mutex> const db_lock(ledger_mutex_);
          auto existing_entries = storage_->GetLedgerEntries(topic_id);
          std::unordered_set<std::string> existing_signatures;
          for (auto const& ex : existing_entries) {
            existing_signatures.insert(
                ex.author_id.ToString() + std::to_string(ex.created_at.value) +
                std::to_string(static_cast<int>(ex.type)) + ex.payload
            );
          }

          for (auto& e : peer_entries) {
            std::string const sig = e.author_id.ToString() + std::to_string(e.created_at.value) +
                                    std::to_string(static_cast<int>(e.type)) + e.payload;
            if (!existing_signatures.contains(sig)) {
              actually_new.push_back(e);
              storage_->AddLedgerEntry(e);
            }
          }

          if (!actually_new.empty()) {
            std::ignore = ValidateAndRepairChain(topic_id);
          }
        }

        {
          std::lock_guard<std::mutex> const slock(sessions_mutex_);
          auto& session = sessions_[peer_id];
          session.active = true;
          for (auto& e : actually_new) {
            session.postponed_events.push_back(e);
          }
        }

        Json entries_json = Json::array();
        if (auto peer = transport_->GetPeer(peer_id); peer && peer->session_cipher->IsReady()) {
          for (auto const& e : my_batch) {
            if (auto encrypted = peer->session_cipher->Encrypt(e.payload); !encrypted.empty()) {
              entries_json.push_back(
                  {{"author", e.author_id.ToString()},
                   {"created", e.created_at.value},
                   {"type", static_cast<int>(e.type)},
                   {"payload", crypto::ToHex(encrypted)},
                   {"prev_hash", e.prev_hash.ToString()},
                   {"hash", e.hash.ToString()}}
              );
            }
          }
        }

        Json const reply = {
            {"type", "sync_batch_reply"},
            {"topic", topic_id.ToString()},
            {"lca", lca.ToString()},
            {"batch_index", batch_index},
            {"total_entries", my_total_entries},
            {"entries", entries_json}
        };
        transport_->SendJson(peer_id, reply);
        EventQueue::Instance().Push(EventSyncStateChanged{peer_id, true, 0.5f});

      } else if (type == "sync_batch_reply") {
        messaging::LedgerHash const lca(msg.value("lca", ""));
        size_t const batch_index = msg.value("batch_index", 0ULL);
        size_t const peer_total_entries = msg.value("total_entries", 0ULL);

        LGI << "[Sync] Received sync_batch_reply from " << peer_id.ToString() << " batch_index: " << batch_index
            << " containing " << msg["entries"].size() << " entries.";

        auto peer_entries = ParseEntries(peer_id, topic_id, msg["entries"]);
        std::vector<messaging::LedgerEntry> actually_new;

        if (!peer_entries.empty()) {
          std::unique_lock<std::mutex> const db_lock(ledger_mutex_);
          auto existing_entries = storage_->GetLedgerEntries(topic_id);
          std::unordered_set<std::string> existing_signatures;
          for (auto const& ex : existing_entries) {
            existing_signatures.insert(
                ex.author_id.ToString() + std::to_string(ex.created_at.value) +
                std::to_string(static_cast<int>(ex.type)) + ex.payload
            );
          }

          for (auto& e : peer_entries) {
            std::string const sig = e.author_id.ToString() + std::to_string(e.created_at.value) +
                                    std::to_string(static_cast<int>(e.type)) + e.payload;
            if (!existing_signatures.contains(sig)) {
              actually_new.push_back(e);
              storage_->AddLedgerEntry(e);
            }
          }

          if (!actually_new.empty()) {
            std::ignore = ValidateAndRepairChain(topic_id);
          }
        }

        bool has_more_batches = false;
        size_t next_batch_index = batch_index + 1;
        std::vector<messaging::LedgerEntry> my_next_batch;
        size_t my_total = 0;

        {
          std::lock_guard<std::mutex> const slock(sessions_mutex_);
          auto& session = sessions_[peer_id];
          session.active = true;
          for (auto& e : actually_new) {
            session.postponed_events.push_back(e);
          }

          session.peer_total_entries = peer_total_entries;
          my_total = session.my_entries_to_send.size();

          if ((next_batch_index * maxBatchSize < my_total) || (next_batch_index * maxBatchSize < peer_total_entries)) {
            has_more_batches = true;
            size_t const start_idx = next_batch_index * maxBatchSize;
            if (start_idx < my_total) {
              size_t const end_idx = std::min<size_t>(start_idx + maxBatchSize, my_total);
              my_next_batch.assign(
                  session.my_entries_to_send.begin() + start_idx,
                  session.my_entries_to_send.begin() + end_idx
              );
            }
          }
        }

        if (has_more_batches) {
          LGI << "[Sync] Sending next batch " << next_batch_index << " out of approx max(" << (my_total / maxBatchSize)
              << ", " << (peer_total_entries / maxBatchSize) << ") for peer " << peer_id.ToString();

          Json entries_json = Json::array();
          if (auto peer = transport_->GetPeer(peer_id); peer && peer->session_cipher->IsReady()) {
            for (auto const& e : my_next_batch) {
              if (auto encrypted = peer->session_cipher->Encrypt(e.payload); !encrypted.empty()) {
                entries_json.push_back(
                    {{"author", e.author_id.ToString()},
                     {"created", e.created_at.value},
                     {"type", static_cast<int>(e.type)},
                     {"payload", crypto::ToHex(encrypted)},
                     {"prev_hash", e.prev_hash.ToString()},
                     {"hash", e.hash.ToString()}}
                );
              }
            }
          }

          Json const req = {
              {"type", "sync_batch"},
              {"topic", topic_id.ToString()},
              {"lca", lca.ToString()},
              {"batch_index", next_batch_index},
              {"total_entries", my_total},
              {"entries", entries_json}
          };
          transport_->SendJson(peer_id, req);
          EventQueue::Instance().Push(EventSyncStateChanged{peer_id, true, 0.5f});
        } else {
          LGI << "[Sync] Batch exchange complete for " << peer_id.ToString() << ". Repeating VerifySyncState.";
          dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { VerifySyncState(topic_id, peer_id); });
        }

      } else if (type == "sync_done") {
        LGI << "[Sync] Received sync_done from " << peer_id.ToString();
        dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { FinishSync(peer_id, topic_id); });
      }
    } catch (std::exception const& e) {
      LGE << "[Sync] Exception in ProcessSyncMessage: " << e.what();
    }
  }

 private:
  void ProcessPendingSyncs() {
    if (!isRunning_) {
      return;
    }
    auto const reqs = storage_->GetSyncRequests();
    std::unordered_set<ContactID> processed;
    for (auto const& req : reqs) {
      if (processed.contains(req.target_peer_id)) {
        continue;
      }
      processed.insert(req.target_peer_id);

      auto contact = storage_->GetContact(req.target_peer_id);
      if (!contact || contact->status != identity::ContactStatus::confirmed) {
        continue;
      }
      if (auto const peer = transport_->GetPeer(req.target_peer_id); peer && peer->data_channel_is_open) {
        if (peer->session_cipher->IsReady() && peer->crcp_verified) {
          ProposeSync(req.topic_id, req.target_peer_id);
        }
      }
    }
  }
};

}  // namespace eillance::sync
