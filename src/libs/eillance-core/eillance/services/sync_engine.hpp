#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

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
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  ContactID my_id_;

  std::atomic<bool> isRunning_{false};
  DispatchQueue dispatchQueue_{"SyncEngine", 1};
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unordered_map<std::string, std::vector<storage::LedgerEntry>> pending_sync_chain_;

  struct SyncRequestState {
    std::string hash;
    Time::Unit timestamp;
  };
  std::unordered_map<std::string, SyncRequestState> active_sync_requests_;
  std::unordered_set<std::string> pending_proposals_;
  std::mutex proposals_mutex_;

  static auto ComputeSHA256(std::string const& data) -> std::string {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length_of_hash = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
    EVP_DigestUpdate(context, data.c_str(), data.length());
    EVP_DigestFinal_ex(context, hash, &length_of_hash);
    EVP_MD_CTX_free(context);
    std::vector<std::byte> const bytes(
        reinterpret_cast<std::byte*>(hash),
        reinterpret_cast<std::byte*>(hash) + length_of_hash
    );
    return crypto::ToHex(bytes);
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
    for (auto id : event_unsubs_) {
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

  void AppendAction(TopicID const& topic_id, storage::LedgerActionType type, nlohmann::json const& payload) {
    storage::LedgerEntry entry;
    entry.topic_id = topic_id;
    entry.author_id = my_id_;
    entry.created_at = Timestamp::Now();
    entry.type = type;
    entry.payload = payload.dump();

    auto latest = storage_->GetLatestLedgerEntry(topic_id);
    entry.prev_hash = latest ? latest->hash : std::string(64, '0');

    std::string const data_to_hash = entry.prev_hash + entry.author_id.ToString() +
                                     std::to_string(entry.created_at.value) +
                                     std::to_string(static_cast<int>(entry.type)) + entry.payload;
    entry.hash = ComputeSHA256(data_to_hash);

    storage_->AddLedgerEntry(entry);
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
      if (type != storage::LedgerActionType::ProfileUpdate) {
        EventQueue::Instance().Push(EventConnectionRequested{target_peer});
      }
    }
  }

  void ProposeSync(TopicID const& topic_id, ContactID const& target_peer) {
    std::string const key = topic_id.ToString() + ":" + target_peer.ToString();
    {
      std::lock_guard<std::mutex> const lock(proposals_mutex_);
      if (pending_proposals_.contains(key)) {
        return;
      }
      pending_proposals_.insert(key);
    }

    dispatchQueue_.DispatchAfter(std::chrono::milliseconds(250), [this, topic_id, target_peer, key]() {
      {
        std::lock_guard<std::mutex> const lock(proposals_mutex_);
        pending_proposals_.erase(key);
      }
      auto const latest = storage_->GetLatestLedgerEntry(topic_id);
      std::string const head = latest ? latest->hash : std::string(64, '0');
      LGI << "[Sync] Proposing sync to " << target_peer.ToString() << " for topic " << topic_id.ToString() << " with head " << head;
      nlohmann::json const msg = {{"type", "sync_propose"}, {"topic", topic_id.ToString()}, {"head", head}};
      transport_->SendJson(target_peer, msg);
    });
  }

  void ProcessSyncMessage(ContactID const& peer_id, nlohmann::json const& msg) {
    try {
      std::string const type = msg.value("type", "");

      // In 1-on-1 P2P chats, the remote peer's "topic" for us is our own ID, but our local
      // topic for this ledger is the remote's peer_id. We map it directly to the sender.
      TopicID const topic_id(peer_id);

      LGI << "[Sync] Received " << type << " from " << peer_id.ToString() << " for topic " << topic_id.ToString();

      if (type == "sync_propose") {
        std::string const peer_head = msg.value("head", "");
        auto latest = storage_->GetLatestLedgerEntry(topic_id);
        std::string const my_head = latest ? latest->hash : std::string(64, '0');

        if (peer_head == my_head) {
          LGI << "[Sync] Peer head matches our head. Sync complete.";
          storage_->RemoveSyncRequest(topic_id, peer_id);
          active_sync_requests_.erase(topic_id.ToString());
          nlohmann::json const confirm = {{"type", "sync_confirm"}, {"topic", topic_id.ToString()}, {"head", my_head}};
          transport_->SendJson(peer_id, confirm);
          return;
        }

        if (storage_->HasLedgerEntry(topic_id, peer_head)) {
          // We have their head, but our head is strictly newer or different. Propose ours.
          ProposeSync(topic_id, peer_id);
          return;
        }

        auto const now = Time::SteadyNow();
        auto it = active_sync_requests_.find(topic_id.ToString());
        if (it != active_sync_requests_.end() && it->second.hash == peer_head) {
          if (now - it->second.timestamp < 5000000ULL) {
            return;
          }
        }
        active_sync_requests_[topic_id.ToString()] = {peer_head, now};

        // Iterative backwards fetch
        pending_sync_chain_[topic_id.ToString()].clear();
        nlohmann::json const req = {
            {"type", "sync_request_entry"},
            {"topic", topic_id.ToString()},
            {"hash", peer_head}
        };
        transport_->SendJson(peer_id, req);
      } else if (type == "sync_request_entry") {
        std::string const req_hash = msg.value("hash", "");
        auto entry_opt = storage_->GetLedgerEntryByHash(topic_id, req_hash);
        if (entry_opt) {
          auto peer = transport_->GetPeer(peer_id);
          if (peer && peer->session_cipher->IsReady()) {
            auto encrypted = peer->session_cipher->Encrypt(entry_opt->payload);
            nlohmann::json const reply = {
                {"type", "sync_provide_entry"},
                {"topic", topic_id.ToString()},
                {"entry",
                 {{"author", entry_opt->author_id.ToString()},
                  {"created", entry_opt->created_at.value},
                  {"type", static_cast<int>(entry_opt->type)},
                  {"payload", crypto::ToHex(encrypted)},
                  {"prev_hash", entry_opt->prev_hash},
                  {"hash", entry_opt->hash}}}
            };
            transport_->SendJson(peer_id, reply);
          }
        } else {
          LGE << "[Sync] Peer requested missing entry: " << req_hash;
          nlohmann::json const reply = {{"type", "sync_not_found"}, {"topic", topic_id.ToString()}, {"hash", req_hash}};
          transport_->SendJson(peer_id, reply);
        }
      } else if (type == "sync_provide_entry") {
        auto peer = transport_->GetPeer(peer_id);
        if (!peer || !peer->session_cipher->IsReady()) return;

        auto e_json = msg["entry"];
        storage::LedgerEntry e;
        e.topic_id = topic_id;
        e.author_id = ContactID(e_json["author"].get<std::string>());
        e.created_at = Timestamp{e_json["created"].get<uint64_t>()};
        e.type = static_cast<storage::LedgerActionType>(e_json["type"].get<int>());
        e.prev_hash = e_json.value("prev_hash", "");
        e.hash = e_json.value("hash", "");

        auto encrypted_bytes = crypto::FromHex(e_json["payload"].get<std::string>());
        if (!encrypted_bytes.empty()) {
          auto decrypted = peer->session_cipher->Decrypt(encrypted_bytes);
          if (!decrypted.empty()) {
            e.payload = decrypted;
          }
        }

        auto& pending = pending_sync_chain_[topic_id.ToString()];
        if (pending.size() > 1000) {
          LGE << "[Sync] Sync chain too long, aborting iterative sync.";
          pending.clear();
          return;
        }
        pending.push_back(e);

        if (e.prev_hash == std::string(64, '0') || storage_->HasLedgerEntry(topic_id, e.prev_hash)) {
          // LCA reached!
          std::string actual_lca = e.prev_hash;
          if (!storage_->HasLedgerEntry(topic_id, actual_lca)) {
            actual_lca = std::string(64, '0');
          }

          auto my_entries = storage_->GetLedgerEntriesAfter(topic_id, actual_lca);

          std::vector<storage::LedgerEntry> peer_entries = pending;
          std::ranges::reverse(peer_entries);
          pending.clear();

          std::vector<storage::LedgerEntry> combined;
          combined.reserve(my_entries.size() + peer_entries.size());
          combined.insert(combined.end(), my_entries.begin(), my_entries.end());
          combined.insert(combined.end(), peer_entries.begin(), peer_entries.end());

          // Deterministic sort resolving "take both"
          std::ranges::sort(combined, [](auto const& a, auto const& b) {
            if (a.created_at.value != b.created_at.value) return a.created_at.value < b.created_at.value;
            if (a.author_id.ToString() != b.author_id.ToString())
              return a.author_id.ToString() < b.author_id.ToString();
            if (a.type != b.type) return a.type < b.type;
            return a.payload < b.payload;
          });

          // Unique deduplication
          auto unique_range = std::ranges::unique(combined, [](auto const& a, auto const& b) {
            return a.created_at.value == b.created_at.value && a.author_id.ToString() == b.author_id.ToString() &&
                   a.type == b.type && a.payload == b.payload;
          });
          combined.erase(unique_range.begin(), unique_range.end());

          std::vector<storage::LedgerEntry> new_entries;
          for (auto const& ce : combined) {
            bool found = false;
            for (auto const& me : my_entries) {
              if (ce.author_id == me.author_id && ce.created_at == me.created_at && ce.type == me.type &&
                  ce.payload == me.payload) {
                found = true;
                break;
              }
            }
            if (!found) new_entries.push_back(ce);
          }

          auto current_hash = actual_lca;
          for (auto& entry : combined) {
            entry.prev_hash = current_hash;
            std::string const data_to_hash = entry.prev_hash + entry.author_id.ToString() +
                                             std::to_string(entry.created_at.value) +
                                             std::to_string(static_cast<int>(entry.type)) + entry.payload;
            entry.hash = ComputeSHA256(data_to_hash);
            current_hash = entry.hash;
          }

          storage_->ReplaceLedgerEntries(topic_id, actual_lca, combined);

          active_sync_requests_.erase(topic_id.ToString());

          for (auto const& entry : new_entries) {
            EventQueue::Instance().Push(EventLedgerAppended{entry});
          }

          ProposeSync(topic_id, peer_id);
        } else {
          nlohmann::json const req = {
              {"type", "sync_request_entry"},
              {"topic", topic_id.ToString()},
              {"hash", e.prev_hash}
          };
          transport_->SendJson(peer_id, req);
        }
      } else if (type == "sync_not_found") {
        active_sync_requests_.erase(topic_id.ToString());
        pending_sync_chain_[topic_id.ToString()].clear();
      } else if (type == "sync_confirm") {
        std::string const peer_head = msg.value("head", "");
        LGI << "[Sync] Sync confirmed by peer with head: " << peer_head;
        auto const latest = storage_->GetLatestLedgerEntry(topic_id);
        std::string const my_head = latest ? latest->hash : std::string(64, '0');
        if (my_head == peer_head) {
          storage_->RemoveSyncRequest(topic_id, peer_id);
          active_sync_requests_.erase(topic_id.ToString());
          pending_sync_chain_[topic_id.ToString()].clear();
        } else {
          LGI << "[Sync] Head mismatch after confirm, requesting sync.";
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
      if (processed.contains(req.target_peer_id)) continue;
      processed.insert(req.target_peer_id);
      
      if (processed.contains(req.target_peer_id)) continue;
      processed.insert(req.target_peer_id);
      auto contact = storage_->GetContact(req.target_peer_id);
      if (!contact || contact->status != identity::ContactStatus::confirmed) {
        continue;
      }
      if (auto const peer = transport_->GetPeer(req.target_peer_id); peer && peer->data_channel_is_open) {
        if (peer->session_cipher->IsReady()) {
          ProposeSync(req.topic_id, req.target_peer_id);
        } else {
          auto const my_pub_key = peer->session_cipher->GetPublicKey();
          nlohmann::json const key_msg = {
              {"type", "key_exchange"},
              {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
              {"is_reply", false}
          };
          transport_->SendJson(req.target_peer_id, key_msg);
        }
      }
    }
  }
};

}  // namespace eillance::sync
