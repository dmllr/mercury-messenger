#pragma once

#include <algorithm>
#include <chrono>
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

  static constexpr auto maxBatchSize = 500U;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  ContactID my_id_;

  std::atomic<bool> isRunning_{false};
  DispatchQueue dispatchQueue_{"SyncEngine", 1};
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unordered_set<std::string> pending_proposals_;
  std::mutex proposals_mutex_;
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
      return a.created_at.value == b.created_at.value && a.author_id == b.author_id && a.type == b.type &&
             a.payload == b.payload;
    });
    entries.erase(unique_range.begin(), unique_range.end());

    auto current_hash = messaging::LedgerHash();
    bool chain_broken = false;
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
    std::lock_guard<std::mutex> const lock(proposals_mutex_);
    pending_proposals_.clear();
  }

  void AppendAction(TopicID const& topic_id, messaging::LedgerActionType type, Json const& payload) {
    messaging::LedgerEntry entry;
    entry.topic_id = topic_id;
    entry.author_id = my_id_;
    entry.created_at = Timestamp::Now();
    entry.type = type;
    entry.payload = payload.dump();

    std::unique_lock<std::mutex> lock(ledger_mutex_);
    auto latest = storage_->GetLatestLedgerEntry(topic_id);

    // Enforce strict local monotonicity for ledger timestamps
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

    EventQueue::Instance().Push(EventLedgerAppended{entry});

    // For 1-on-1 chats, the target peer is the TopicID.
    ContactID const target_peer(topic_id.value.Span());
    storage_->AddSyncRequest({topic_id, target_peer, Timestamp::Now(), Timestamp{0}});

    auto contact = storage_->GetContact(target_peer);
    if (!contact || contact->status != identity::ContactStatus::confirmed) {
      LGE << "Sync error/rejection: Contact " << target_peer.ToString() << " is not confirmed.";
      return;
    }

    if (auto peer = transport_->GetPeer(target_peer); peer && peer->data_channel_is_open) {
      ProposeSync(topic_id, target_peer);
    } else {
      if (type != messaging::LedgerActionType::ProfileUpdate) {
        EventQueue::Instance().Push(EventConnectionRequested{target_peer});
      }
    }
  }

  void ProposeSync(TopicID const& topic_id, ContactID const& target_peer) {
    std::string const key = topic_id.ToString() + ":" + target_peer.ToString();
    {
      std::unique_lock<std::mutex> const lock(ledger_mutex_);
      std::ignore = ValidateAndRepairChain(topic_id);
    }
    {
      std::lock_guard<std::mutex> const lock(proposals_mutex_);
      if (pending_proposals_.contains(key)) {
        return;
      }
      pending_proposals_.insert(key);
    }

    dispatchQueue_.DispatchAfter(std::chrono::milliseconds(250), [this, topic_id, target_peer, key]() -> void {
      {
        std::lock_guard<std::mutex> const lock(proposals_mutex_);
        pending_proposals_.erase(key);
      }
      messaging::LedgerHash head;
      size_t size = 0;
      {
        std::unique_lock<std::mutex> const lock(ledger_mutex_);
        auto const [fst, snd] = ValidateAndRepairChain(topic_id);
        head = fst;
        size = snd;
      }
      LGI << "[Sync] Proposing sync to " << target_peer.ToString() << " for topic " << topic_id.ToString()
          << " with head " << head.ToString() << " and size " << size;
      Json const msg =
          {{"type", "sync_propose"}, {"topic", topic_id.ToString()}, {"head", head.ToString()}, {"size", size}};
      transport_->SendJson(target_peer, msg);
    });
  }

  void ProcessSyncMessage(ContactID const& peer_id, Json const& msg) {
    try {
      std::string const type = msg.value("type", "");

      // In 1-on-1 P2P chats, the remote peer's "topic" for us is our own ID, but our local
      // topic for this ledger is the remote's peer_id. We map it directly to the sender.
      TopicID const topic_id(peer_id);

      if (type == "sync_propose") {
        messaging::LedgerHash const peer_head = msg.value("head", messaging::LedgerHash{});
        size_t const peer_size = msg.value("size", static_cast<size_t>(0));
        std::unique_lock<std::mutex> lock(ledger_mutex_);

        auto const [my_head, my_size] = ValidateAndRepairChain(topic_id);

        if (peer_head == my_head) {
          if (peer_size != 0 && peer_size != my_size) {
            LGE << "[Sync] CRITICAL: Same head but different size! peer_size=" << peer_size << " my_size=" << my_size;
          } else {
            storage_->RemoveSyncRequest(topic_id, peer_id);
            lock.unlock();
            Json const confirm = {
                {"type", "sync_confirm"},
                {"topic", topic_id.ToString()},
                {"head", my_head.ToString()},
                {"size", my_size}
            };
            transport_->SendJson(peer_id, confirm);
            EventQueue::Instance().Push(EventSyncStateChanged{peer_id, false, 1.0f});
            return;
          }
        }

        if (storage_->HasLedgerEntry(topic_id, peer_head)) {
          lock.unlock();
          ProposeSync(topic_id, peer_id);
          return;
        }

        std::vector<messaging::LedgerHash> checkpoints;
        messaging::LedgerHash current_hash = my_head;
        int skip = 1;
        int next_checkpoint = 0;
        int index = 0;
        while (!current_hash.IsEmpty()) {
          if (index == next_checkpoint) {
            checkpoints.push_back(current_hash);
            if (checkpoints.size() >= 32) {
              break;
            }
            skip *= 2;
            next_checkpoint += skip;
          }
          auto entry = storage_->GetLedgerEntryByHash(topic_id, current_hash);
          if (!entry) {
            break;
          }
          current_hash = entry->prev_hash;
          index++;
        }
        if (checkpoints.empty() || !checkpoints.back().IsEmpty()) {
          checkpoints.emplace_back();
        }

        lock.unlock();
        Json const req = {
            {"type", "sync_request_forward"},
            {"topic", topic_id.ToString()},
            {"checkpoints", checkpoints}
        };
        transport_->SendJson(peer_id, req);
        EventQueue::Instance().Push(EventSyncStateChanged{peer_id, true, -1.0f});
      } else if (type == "sync_request_forward") {
        auto checkpoints = msg.value("checkpoints", std::vector<messaging::LedgerHash>{});
        std::unique_lock<std::mutex> lock(ledger_mutex_);
        messaging::LedgerHash lca;
        for (auto const& cp : checkpoints) {
          if (cp.IsEmpty() || storage_->HasLedgerEntry(topic_id, cp)) {
            lca = cp;
            break;
          }
        }

        if (!checkpoints.empty() && lca != checkpoints.front()) {
          // Fork detected
          Json const reply = {{"type", "sync_fork_detected"}, {"topic", topic_id.ToString()}, {"lca", lca.ToString()}};
          transport_->SendJson(peer_id, reply);

          auto entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
          lock.unlock();

          if (!entries.empty()) {
            size_t batch_size = maxBatchSize;
            while (batch_size > 0) {
              size_t const actual_size = std::min(batch_size, entries.size());
              Json entries_json = Json::array();
              auto peer_transport = transport_->GetPeer(peer_id);
              if (!peer_transport || !peer_transport->session_cipher->IsReady()) {
                break;
              }

              for (size_t i = 0; i < actual_size; ++i) {
                auto const& entry = entries[i];
                auto encrypted = peer_transport->session_cipher->Encrypt(entry.payload);
                entries_json.push_back(
                    {{"author", entry.author_id.ToString()},
                     {"created", entry.created_at.value},
                     {"type", static_cast<int>(entry.type)},
                     {"payload", crypto::ToHex(encrypted)},
                     {"prev_hash", entry.prev_hash.ToString()},
                     {"hash", entry.hash.ToString()}}
                );
              }

              Json const fork_reply = {
                  {"type", "sync_provide_fork"},
                  {"topic", topic_id.ToString()},
                  {"entries", entries_json}
              };

              if (transport_->SendJson(peer_id, fork_reply)) {
                LGI << "[Sync] Sent fork of " << actual_size << " entries.";
                break;
              }
              batch_size /= 2;
            }
          }
          return;
        }

        auto entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
        lock.unlock();

        if (entries.empty()) {
          return;
        }

        size_t batch_size = maxBatchSize;
        while (batch_size > 0) {
          size_t const actual_size = std::min(batch_size, entries.size());
          Json entries_json = Json::array();
          auto peer_transport = transport_->GetPeer(peer_id);
          if (!peer_transport || !peer_transport->session_cipher->IsReady()) {
            return;
          }

          for (size_t i = 0; i < actual_size; ++i) {
            auto const& e = entries[i];
            auto encrypted = peer_transport->session_cipher->Encrypt(e.payload);
            entries_json.push_back(
                {{"author", e.author_id.ToString()},
                 {"created", e.created_at.value},
                 {"type", static_cast<int>(e.type)},
                 {"payload", crypto::ToHex(encrypted)},
                 {"prev_hash", e.prev_hash.ToString()},
                 {"hash", e.hash.ToString()}}
            );
          }

          Json const reply = {
              {"type", "sync_provide_forward_batch"},
              {"topic", topic_id.ToString()},
              {"entries", entries_json},
              {"has_more", actual_size < entries.size()}
          };

          if (transport_->SendJson(peer_id, reply)) {
            LGI << "[Sync] Sent forward batch of " << actual_size << " entries.";
            break;
          }
          batch_size /= 2;
        }
      } else if (type == "sync_fork_detected") {
        messaging::LedgerHash const lca = msg.value("lca", messaging::LedgerHash{});
        std::unique_lock<std::mutex> lock(ledger_mutex_);
        auto entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
        lock.unlock();

        if (entries.empty()) {
          return;
        }

        size_t batch_size = maxBatchSize;
        while (batch_size > 0) {
          auto const actual_size = std::min(batch_size, entries.size());
          Json entries_json = Json::array();
          auto peer_transport = transport_->GetPeer(peer_id);
          if (!peer_transport || !peer_transport->session_cipher->IsReady()) {
            return;
          }

          for (size_t i = 0; i < actual_size; ++i) {
            auto const& e = entries[i];
            auto encrypted = peer_transport->session_cipher->Encrypt(e.payload);
            entries_json.push_back(
                {{"author", e.author_id.ToString()},
                 {"created", e.created_at.value},
                 {"type", static_cast<int>(e.type)},
                 {"payload", crypto::ToHex(encrypted)},
                 {"prev_hash", e.prev_hash.ToString()},
                 {"hash", e.hash.ToString()}}
            );
          }

          Json const reply = {{"type", "sync_provide_fork"}, {"topic", topic_id.ToString()}, {"entries", entries_json}};

          if (transport_->SendJson(peer_id, reply)) {
            LGI << "[Sync] Sent fork of " << actual_size << " entries.";
            break;
          }
          batch_size /= 2;
        }
      } else if (type == "sync_provide_fork" || type == "sync_provide_forward_batch") {
        auto parse_entries = [&](Json const& msg_entries) -> std::vector<messaging::LedgerEntry> {
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

            auto const encrypted_bytes = crypto::FromHex(e_json["payload"].get<std::string>());
            if (!encrypted_bytes.empty()) {
              auto const decrypted = peer->session_cipher->Decrypt(encrypted_bytes);
              if (!decrypted.empty()) {
                e.payload = decrypted;
                parsed.push_back(e);
              } else {
                LGE << "[Sync] Decryption failed for entry " << e.hash.ToString();
              }
            }
          }
          return parsed;
        };

        auto merge_entries = [&](std::vector<messaging::LedgerEntry> peer_entries) -> void {
          if (peer_entries.empty()) {
            return;
          }
          messaging::LedgerHash actual_lca = peer_entries.front().prev_hash;
          if (!storage_->HasLedgerEntry(topic_id, actual_lca)) {
            actual_lca = messaging::LedgerHash();
          }

          auto my_entries = storage_->GetLedgerEntriesAfter(topic_id, actual_lca);

          std::vector<messaging::LedgerEntry> combined;
          combined.reserve(my_entries.size() + peer_entries.size());
          combined.insert(combined.end(), my_entries.begin(), my_entries.end());
          combined.insert(combined.end(), peer_entries.begin(), peer_entries.end());

          std::ranges::sort(combined, [](auto const& a, auto const& b) {
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

          auto unique_range = std::ranges::unique(combined, [](auto const& a, auto const& b) {
            return a.created_at.value == b.created_at.value && a.author_id == b.author_id && a.type == b.type &&
                   a.payload == b.payload;
          });
          combined.erase(unique_range.begin(), unique_range.end());

          std::vector<messaging::LedgerEntry> new_entries;
          for (auto const& ce : combined) {
            bool found = false;
            for (auto const& me : my_entries) {
              if (ce.author_id == me.author_id && ce.created_at.value == me.created_at.value && ce.type == me.type &&
                  ce.payload == me.payload) {
                found = true;
                break;
              }
            }
            if (!found) {
              new_entries.push_back(ce);
            }
          }

          auto current_hash = actual_lca;
          for (auto& entry : combined) {
            entry.prev_hash = current_hash;
            std::string const data_to_hash = entry.prev_hash.ToString() + entry.author_id.ToString() +
                                             std::to_string(entry.created_at.value) +
                                             std::to_string(static_cast<int>(entry.type)) + entry.payload;
            entry.hash = ComputeSHA256(data_to_hash);
            current_hash = entry.hash;
          }

          storage_->ReplaceLedgerEntries(topic_id, actual_lca, combined);

          for (auto const& entry : new_entries) {
            EventQueue::Instance().Push(EventLedgerAppended{entry});
          }
        };

        if (type == "sync_provide_fork") {
          auto peer_entries = parse_entries(msg["entries"]);
          if (peer_entries.empty()) {
            return;
          }

          std::unique_lock<std::mutex> lock(ledger_mutex_);
          merge_entries(peer_entries);
          lock.unlock();

          ProposeSync(topic_id, peer_id);
        } else if (type == "sync_provide_forward_batch") {
          auto peer_entries = parse_entries(msg["entries"]);
          if (peer_entries.empty()) {
            storage_->RemoveSyncRequest(topic_id, peer_id);
            return;
          }

          std::unique_lock<std::mutex> lock(ledger_mutex_);
          merge_entries(peer_entries);

          bool const has_more = msg.value("has_more", false);
          if (has_more) {
            auto const result = ValidateAndRepairChain(topic_id);
            messaging::LedgerHash const my_head = result.first;

            std::vector<messaging::LedgerHash> checkpoints;
            messaging::LedgerHash current_hash = my_head;
            int skip = 1;
            int next_checkpoint = 0;
            int index = 0;
            while (!current_hash.IsEmpty()) {
              if (index == next_checkpoint) {
                checkpoints.push_back(current_hash);
                if (checkpoints.size() >= 32) {
                  break;
                }
                skip *= 2;
                next_checkpoint += skip;
              }
              auto entry = storage_->GetLedgerEntryByHash(topic_id, current_hash);
              if (!entry) {
                break;
              }
              current_hash = entry->prev_hash;
              index++;
            }
            if (checkpoints.empty() || !checkpoints.back().IsEmpty()) {
              checkpoints.emplace_back();
            }
            lock.unlock();

            Json const req = {
                {"type", "sync_request_forward"},
                {"topic", topic_id.ToString()},
                {"checkpoints", checkpoints}
            };
            transport_->SendJson(peer_id, req);
            EventQueue::Instance().Push(EventSyncStateChanged{peer_id, true, -1.0f});
          } else {
            auto const result = ValidateAndRepairChain(topic_id);
            messaging::LedgerHash const my_head = result.first;
            size_t const my_size = result.second;
            lock.unlock();

            Json const confirm = {
                {"type", "sync_confirm"},
                {"topic", topic_id.ToString()},
                {"head", my_head.ToString()},
                {"size", my_size}
            };
            transport_->SendJson(peer_id, confirm);
            storage_->RemoveSyncRequest(topic_id, peer_id);
            EventQueue::Instance().Push(EventSyncStateChanged{peer_id, false, 1.0f});
          }
        }
      } else if (type == "sync_confirm") {
        messaging::LedgerHash const peer_head = msg.value("head", messaging::LedgerHash{});
        size_t const peer_size = msg.value("size", static_cast<size_t>(0));
        std::unique_lock<std::mutex> lock(ledger_mutex_);
        auto const result = ValidateAndRepairChain(topic_id);
        messaging::LedgerHash const my_head = result.first;
        size_t const my_size = result.second;

        if (my_head == peer_head) {
          if (peer_size != 0 && peer_size != my_size) {
            LGE << "[Sync] CRITICAL: Same head but different size! peer_size=" << peer_size << " my_size=" << my_size;
          }
          storage_->RemoveSyncRequest(topic_id, peer_id);
          EventQueue::Instance().Push(EventSyncStateChanged{peer_id, false, 1.0f});
        } else {
          lock.unlock();
          ProposeSync(topic_id, peer_id);
        }
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
