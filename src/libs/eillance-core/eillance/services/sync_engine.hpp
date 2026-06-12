#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/timestamp.hpp"

namespace eillance::sync {

class SyncEngine {
  using Json = nlohmann::json;

  static constexpr size_t maxBatchSize = 1;
  static constexpr size_t syncWindowSize = 15;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  ContactID my_id_;

  std::atomic<bool> isRunning_{false};
  DispatchQueue dispatchQueue_{"SyncEngine", 1};
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  struct SyncSession {
    TopicID topic_id;
    ContactID peer_id;
    bool active = false;
    messaging::LedgerHash current_lca;

    int zero_progress_loops = 0;
    messaging::LedgerHash last_head;

    std::vector<messaging::LedgerEntry> my_entries_to_send;
    size_t peer_total_entries = 0;
    size_t current_batch_index = 0;
    size_t highest_sent_batch_index = 0;
    bool pending_request = false;
    Time::Unit last_activity = 0;
  };

  std::unordered_map<std::string, SyncSession> sessions_;
  std::mutex sessions_mutex_;
  std::mutex ledger_mutex_;

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
      dispatchQueue_.Dispatch([this, master_id = e.peer_id]() -> void {
        if (master_id == my_id_) {
          return;
        }

        std::vector<ContactID> active_devices;
        for (auto const& [net_id, peer] : transport_->GetPeers()) {
          if (transport_->GetMasterId(net_id) == master_id) {
            if (peer->data_channel_is_open && peer->session_cipher->IsReady() && peer->crcp_verified) {
              active_devices.push_back(net_id);
            }
          }
        }

        if (active_devices.empty()) {
          return;
        }

        for (auto const& dev_id : active_devices) {
          ProposeSync(TopicID(master_id), dev_id);
        }

        auto const groups = storage_->GetGroups();
        for (auto const& g : groups) {
          bool is_member = false;
          for (auto const& m : g.members) {
            if (m.contact_id == master_id) {
              is_member = true;
              break;
            }
          }
          if (is_member) {
            for (auto const& dev_id : active_devices) ProposeSync(TopicID(g.id), dev_id);
          }
        }

        auto const channels = storage_->GetChannels();
        for (auto const& c : channels) {
          bool is_member = false;
          for (auto const& m : c.members) {
            if (m.contact_id == master_id) {
              is_member = true;
              break;
            }
          }
          if (is_member) {
            for (auto const& dev_id : active_devices) ProposeSync(TopicID(c.id), dev_id);
          }
        }

        auto const reqs = storage_->GetSyncRequests();
        for (auto const& req : reqs) {
          if (req.target_peer_id == master_id) {
            for (auto const& dev_id : active_devices) ProposeSync(req.topic_id, dev_id);
          }
        }
      });
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSelfDeviceConnected>([this](auto const& e) -> void {
      dispatchQueue_.Dispatch([this, device_id = e.device_id]() -> void {
        ProposeSync(TopicID(my_id_), device_id);

        for (auto const& c : storage_->GetContacts()) {
          ProposeSync(TopicID(c.peer_id), device_id);
        }

        for (auto const& g : storage_->GetGroups()) {
          ProposeSync(TopicID(g.id), device_id);
        }

        for (auto const& ch : storage_->GetChannels()) {
          ProposeSync(TopicID(ch.id), device_id);
        }
      });
    }));

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataPeerConnectionStateChanged>([this](auto const& e) -> void {
          if (e.state == transport::PeerConnectionState::disconnected ||
              e.state == transport::PeerConnectionState::failed || e.state == transport::PeerConnectionState::closed) {
            std::lock_guard<std::mutex> const lock(sessions_mutex_);
            for (auto& [key, session] : sessions_) {
              if (session.peer_id == e.peer_id) {
                session.active = false;
              }
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

    ContactID const target_peer = ContactID(topic_id.Span());

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

    EventQueue::Instance().Push(EventLedgerAppended{entry, false});

    auto group = storage_->GetGroup(GroupID(topic_id.Span()));
    if (group) {
      SendSyncRequestsToMembers(topic_id, my_id_, group->members);
    } else if (auto channel = storage_->GetChannel(ChannelID(topic_id.Span()))) {
      if (channel->owner_id == my_id_) {
        SendSyncRequestsToMembers(topic_id, my_id_, channel->members);
      } else {
        storage_->AddSyncRequest({topic_id, channel->owner_id, Timestamp::Now(), Timestamp{0}});
        bool proposed = false;
        for (auto const& [net_id, peer] : transport_->GetPeers()) {
          if (transport_->GetMasterId(net_id) == channel->owner_id) {
            if (peer->data_channel_is_open && peer->crcp_verified) {
              ProposeSync(topic_id, net_id);
              proposed = true;
            }
          }
        }
        if (!proposed) {
          EventQueue::Instance().Push(EventDeliveryRequested{channel->owner_id});
        }
      }
    } else {
      if (target_peer != my_id_) {
        storage_->AddSyncRequest({topic_id, target_peer, Timestamp::Now(), Timestamp{0}});

        auto contact = storage_->GetContact(target_peer);
        if (!contact || contact->status != identity::ContactStatus::confirmed) {
          LGE << "Sync error/rejection: Contact " << target_peer.ToString() << " is not confirmed.";
        } else {
          bool proposed = false;
          for (auto const& [net_id, peer] : transport_->GetPeers()) {
            if (transport_->GetMasterId(net_id) == target_peer) {
              if (peer->data_channel_is_open && peer->crcp_verified) {
                ProposeSync(topic_id, net_id);
                proposed = true;
              }
            }
          }
          if (!proposed) {
            if (type != messaging::LedgerActionType::ProfileUpdate) {
              EventQueue::Instance().Push(EventDeliveryRequested{target_peer});
            }
          }
        }
      }
    }

    storage_->AddSyncRequest({topic_id, my_id_, Timestamp::Now(), Timestamp{0}});
    bool proposed_self = false;
    for (auto const& [net_id, peer] : transport_->GetPeers()) {
      if (transport_->GetMasterId(net_id) == my_id_) {
        if (peer->data_channel_is_open && peer->crcp_verified) {
          ProposeSync(topic_id, net_id);
          proposed_self = true;
        }
      }
    }
    if (!proposed_self) {
      EventQueue::Instance().Push(EventDeliveryRequested{my_id_});
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
      auto& session = sessions_[GetSessionKey(topic_id, target_peer)];
      if (session.active) {
        if (Time::SteadyNow() - session.last_activity > 15ULL * 1000000ULL) {
          LGE << "[Sync] Session stuck for too long, forcing restart.";
        } else {
          return;
        }
      }
      session.topic_id = topic_id;
      session.peer_id = target_peer;
      session.active = true;
      session.pending_request = false;
      session.last_activity = Time::SteadyNow();
      session.last_head = messaging::LedgerHash();
    }

    ContactID const master_id = transport_->GetMasterId(target_peer);
    EventQueue::Instance().Push(EventSyncStateChanged{master_id, true, -1.0f});

    dispatchQueue_.Dispatch([this, topic_id, target_peer]() -> void { VerifySyncState(topic_id, target_peer); });
  }

  void ProcessSyncMessage(ContactID const& peer_id, Json const& msg) {
    try {
      std::string const type = msg.value("type", "");
      std::string const topic_str = msg.value("topic", transport_->GetMasterId(peer_id).ToString());
      TopicID topic_id(topic_str);
      if (topic_id == TopicID(my_id_)) {
        topic_id = TopicID(transport_->GetMasterId(peer_id));
      }
      std::string const session_key = GetSessionKey(topic_id, peer_id);

      {
        std::lock_guard<std::mutex> const slock(sessions_mutex_);
        sessions_[session_key].last_activity = Time::SteadyNow();
      }

      if (type == "sync_start") {
        ProcessSyncStart(peer_id, topic_id, session_key, msg);
      } else if (type == "sync_checkpoints") {
        ProcessSyncCheckpoints(peer_id, topic_id, session_key, msg);
      } else if (type == "sync_batch") {
        ProcessSyncBatch(peer_id, topic_id, session_key, msg);
      } else if (type == "sync_batch_reply") {
        ProcessSyncBatchReply(peer_id, topic_id, session_key, msg);
      } else if (type == "sync_done") {
        LGI << "[Sync] Received sync_done from " << peer_id.ToString();
        dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { FinishSync(peer_id, topic_id); });
      }
    } catch (std::exception const& e) {
      LGE << "[Sync] Exception in ProcessSyncMessage: " << e.what();
    }
  }

  void AbortSync(ContactID const& target_master_id) {
    bool found_active = false;
    {
      std::lock_guard<std::mutex> const lock(sessions_mutex_);
      for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (transport_->GetMasterId(it->second.peer_id) == target_master_id) {
          if (it->second.active) {
            found_active = true;
          }
          it = sessions_.erase(it);
        } else {
          ++it;
        }
      }
    }

    auto reqs = storage_->GetSyncRequests();
    for (auto const& req : reqs) {
      if (req.target_peer_id == target_master_id) {
        found_active = true;
      }
    }

    if (found_active) {
      EventQueue::Instance().Push(EventSyncStateChanged{target_master_id, false, 1.0f});
    }
  }

 private:
  template <typename MemberType>
  void
  SendSyncRequestsToMembers(TopicID const& topic_id, ContactID const& my_id, std::vector<MemberType> const& members) {
    for (auto const& member : members) {
      if (member.contact_id != my_id) {
        storage_->AddSyncRequest({topic_id, member.contact_id, Timestamp::Now(), Timestamp{0}});
        bool proposed = false;
        for (auto const& [net_id, peer] : transport_->GetPeers()) {
          if (transport_->GetMasterId(net_id) == member.contact_id) {
            if (peer->data_channel_is_open && peer->crcp_verified) {
              ProposeSync(topic_id, net_id);
              proposed = true;
            }
          }
        }
        if (!proposed) {
          EventQueue::Instance().Push(EventDeliveryRequested{member.contact_id});
        }
      }
    }
  }

  template <typename MemberType>
  void
  ForwardToMembers(TopicID const& topic_id, ContactID const& from_master_id, std::vector<MemberType> const& members) {
    for (auto const& member : members) {
      if (member.contact_id != my_id_ && member.contact_id != from_master_id) {
        storage_->AddSyncRequest({topic_id, member.contact_id, Timestamp::Now(), Timestamp{0}});
        bool proposed = false;
        for (auto const& [net_id, peer] : transport_->GetPeers()) {
          if (transport_->GetMasterId(net_id) == member.contact_id) {
            if (peer->data_channel_is_open && peer->crcp_verified) {
              ProposeSync(topic_id, net_id);
              proposed = true;
            }
          }
        }
        if (!proposed) {
          EventQueue::Instance().Push(EventDeliveryRequested{member.contact_id});
        }
      }
    }
  }

  void ForwardNewEntries(TopicID const& topic_id, ContactID const& from_network_id) {
    ContactID const from_master_id = transport_->GetMasterId(from_network_id);

    storage_->AddSyncRequest({topic_id, my_id_, Timestamp::Now(), Timestamp{0}});
    for (auto const& [net_id, peer] : transport_->GetPeers()) {
      if (net_id == from_network_id) {
        continue;
      }
      if (transport_->GetMasterId(net_id) == my_id_) {
        if (peer->data_channel_is_open && peer->session_cipher->IsReady() && peer->crcp_verified) {
          ProposeSync(topic_id, net_id);
        }
      }
    }

    auto group = storage_->GetGroup(GroupID(topic_id.Span()));
    if (group) {
      ForwardToMembers(topic_id, from_master_id, group->members);
    } else if (auto channel = storage_->GetChannel(ChannelID(topic_id.Span()));
               channel && channel->owner_id == my_id_) {
      ForwardToMembers(topic_id, from_master_id, channel->members);
    }
  }

  auto GetSessionKey(TopicID const& topic_id, ContactID const& peer_id) const -> std::string {
    return topic_id.ToString() + "_" + peer_id.ToString();
  }

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
    bool any_active = false;
    bool has_pending = false;
    {
      std::lock_guard<std::mutex> const lock(sessions_mutex_);
      auto& session = sessions_[GetSessionKey(topic_id, peer_id)];
      session.active = false;
      session.zero_progress_loops = 0;
      session.last_head = messaging::LedgerHash();

      if (session.pending_request) {
        has_pending = true;
        session.pending_request = false;
      }

      for (auto const& [key, s] : sessions_) {
        if (s.peer_id == peer_id && s.active) {
          any_active = true;
          break;
        }
      }
    }

    ContactID const master_id = transport_->GetMasterId(peer_id);

    if (has_pending) {
      dispatchQueue_.Dispatch([this, topic_id, peer_id]() -> void { ProposeSync(topic_id, peer_id); });
      if (!any_active) {
        EventQueue::Instance().Push(EventSyncStateChanged{master_id, false, 1.0f});
      }
      return;
    }

    storage_->RemoveSyncRequest(topic_id, master_id);

    if (!any_active) {
      EventQueue::Instance().Push(EventSyncStateChanged{master_id, false, 1.0f});
    }
    EventQueue::Instance().Push(EventDataReloaded{});
    LGI << "[Sync] Sync finished with " << peer_id.ToString();
    dispatchQueue_.Dispatch([this]() -> void { ProcessPendingSyncs(); });
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

  auto ProcessReceivedEntries(ContactID const& peer_id, TopicID const& topic_id, Json const& msg_entries)
      -> std::vector<messaging::LedgerEntry> {
    auto peer_entries = ParseEntries(peer_id, topic_id, msg_entries);
    std::vector<messaging::LedgerEntry> actually_new;
    if (peer_entries.empty()) {
      return actually_new;
    }

    std::unique_lock<std::mutex> const db_lock(ledger_mutex_);
    auto existing_entries = storage_->GetLedgerEntries(topic_id);
    std::unordered_set<std::string> existing_signatures;
    for (auto const& ex : existing_entries) {
      existing_signatures.insert(
          ex.author_id.ToString() + std::to_string(ex.created_at.value) + std::to_string(static_cast<int>(ex.type)) +
          ex.payload
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
    return actually_new;
  }

  void SendSyncBatches(
      ContactID const& peer_id,
      TopicID const& topic_id,
      std::string const& session_key,
      messaging::LedgerHash const& lca,
      size_t my_total,
      std::vector<size_t> const& batches_to_send
  ) {
    for (size_t next_batch_index : batches_to_send) {
      std::vector<messaging::LedgerEntry> my_next_batch;
      {
        std::lock_guard<std::mutex> const slock(sessions_mutex_);
        auto& session = sessions_[session_key];
        size_t const start_idx = next_batch_index * maxBatchSize;
        if (start_idx < session.my_entries_to_send.size()) {
          size_t const end_idx = std::min<size_t>(start_idx + maxBatchSize, session.my_entries_to_send.size());
          my_next_batch.assign(
              session.my_entries_to_send.begin() + start_idx,
              session.my_entries_to_send.begin() + end_idx
          );
        }
      }

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

      {
        std::lock_guard<std::mutex> const slock(sessions_mutex_);
        sessions_[session_key].highest_sent_batch_index = next_batch_index;
      }
    }
  }

  void
  ProcessSyncStart(ContactID const& peer_id, TopicID const& topic_id, std::string const& session_key, Json const& msg) {
    LGD << "[Sync] Received sync_start from " << peer_id.ToString() << " for topic " << topic_id.ToString()
        << " with head " << msg.value("head", "");
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
      auto& session = sessions_[session_key];
      session.active = true;
      session.last_head = peer_head;
    }

    ContactID const master_id = transport_->GetMasterId(peer_id);
    EventQueue::Instance().Push(EventSyncStateChanged{master_id, true, -1.0f});

    Json const req = {{"type", "sync_checkpoints"}, {"topic", topic_id.ToString()}, {"checkpoints", checkpoints}};
    transport_->SendJson(peer_id, req);
  }

  void ProcessSyncCheckpoints(
      ContactID const& peer_id,
      TopicID const& topic_id,
      std::string const& session_key,
      Json const& msg
  ) {
    LGD << "[Sync] Received sync_checkpoints from " << peer_id.ToString() << " for topic " << topic_id.ToString();
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

    LGI << "[Sync] Found LCA " << lca.ToString() << " for peer " << peer_id.ToString() << " with " << my_entries.size()
        << " entries to send.";

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

    size_t my_total = my_entries.size();
    {
      std::lock_guard<std::mutex> const slock(sessions_mutex_);
      auto& session = sessions_[session_key];
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
      session.highest_sent_batch_index = 0;
    }

    size_t initial_batches = (my_total + maxBatchSize - 1) / maxBatchSize;
    if (initial_batches == 0) initial_batches = 1;
    initial_batches = std::min<size_t>(syncWindowSize, initial_batches);

    std::vector<size_t> batches;
    for (size_t i = 0; i < initial_batches; ++i) {
      batches.push_back(i);
    }
    SendSyncBatches(peer_id, topic_id, session_key, lca, my_total, batches);
  }

  void
  ProcessSyncBatch(ContactID const& peer_id, TopicID const& topic_id, std::string const& session_key, Json const& msg) {
    messaging::LedgerHash const lca(msg.value("lca", ""));
    size_t const batch_index = msg.value("batch_index", 0ULL);
    size_t const peer_total_entries = msg.value("total_entries", 0ULL);

    LGI << "[Sync] Received sync_batch from " << peer_id.ToString() << " for topic " << topic_id.ToString()
        << " batch_index: " << batch_index << " containing " << msg["entries"].size() << " entries.";

    auto actually_new = ProcessReceivedEntries(peer_id, topic_id, msg["entries"]);

    std::vector<messaging::LedgerEntry> my_batch;
    size_t my_total_entries = 0;

    {
      std::lock_guard<std::mutex> const slock(sessions_mutex_);
      auto& session = sessions_[session_key];
      session.active = true;

      if (batch_index == 0) {
        std::unique_lock<std::mutex> const db_lock(ledger_mutex_);
        auto my_entries = storage_->GetLedgerEntriesAfter(topic_id, lca);
        std::ranges::sort(my_entries, [](auto const& a, auto const& b) -> bool {
          if (a.created_at.value != b.created_at.value) return a.created_at.value < b.created_at.value;
          if (a.author_id != b.author_id) return a.author_id < b.author_id;
          if (a.type != b.type) return a.type < b.type;
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
        my_batch.assign(session.my_entries_to_send.begin() + start_idx, session.my_entries_to_send.begin() + end_idx);
      }
    }

    for (auto const& e : actually_new) {
      EventQueue::Instance().Push(EventLedgerAppended{e, true});
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
    ContactID const master_id = transport_->GetMasterId(peer_id);

    size_t const max_total_entries = std::max(my_total_entries, peer_total_entries);
    size_t const total_batches = (max_total_entries + maxBatchSize - 1) / maxBatchSize;
    size_t const limit = (total_batches == 0) ? 1 : total_batches;
    float const progress = std::clamp(static_cast<float>(batch_index + 1) / static_cast<float>(limit), 0.0f, 1.0f);

    EventQueue::Instance().Push(EventSyncStateChanged{master_id, true, progress});

    if (!actually_new.empty()) {
      ForwardNewEntries(topic_id, peer_id);
    }
  }

  void ProcessSyncBatchReply(
      ContactID const& peer_id,
      TopicID const& topic_id,
      std::string const& session_key,
      Json const& msg
  ) {
    messaging::LedgerHash const lca(msg.value("lca", ""));
    size_t const batch_index = msg.value("batch_index", 0ULL);
    size_t const peer_total_entries = msg.value("total_entries", 0ULL);

    LGI << "[Sync] Received sync_batch_reply from " << peer_id.ToString() << " for topic " << topic_id.ToString()
        << " batch_index: " << batch_index << " containing " << msg["entries"].size() << " entries.";

    auto actually_new = ProcessReceivedEntries(peer_id, topic_id, msg["entries"]);

    for (auto const& e : actually_new) {
      EventQueue::Instance().Push(EventLedgerAppended{e, true});
    }

    if (!actually_new.empty()) {
      ForwardNewEntries(topic_id, peer_id);
    }

    size_t my_total = 0;
    size_t highest_sent = 0;

    {
      std::lock_guard<std::mutex> const slock(sessions_mutex_);
      auto& session = sessions_[session_key];
      session.active = true;
      session.peer_total_entries = peer_total_entries;
      my_total = session.my_entries_to_send.size();
      highest_sent = session.highest_sent_batch_index;
    }

    size_t const max_total_entries = std::max(my_total, peer_total_entries);
    size_t const total_batches = (max_total_entries + maxBatchSize - 1) / maxBatchSize;
    size_t const limit = (total_batches == 0) ? 1 : total_batches;

    float const progress = std::clamp(static_cast<float>(batch_index + 1) / static_cast<float>(limit), 0.0f, 1.0f);
    ContactID const master_id = transport_->GetMasterId(peer_id);
    EventQueue::Instance().Push(EventSyncStateChanged{master_id, true, progress});

    if (batch_index + 1 >= limit) {
      LGI << "[Sync] Batch exchange complete for " << peer_id.ToString() << ". Repeating VerifySyncState.";
      dispatchQueue_.Dispatch([this, peer_id, topic_id]() -> void { VerifySyncState(topic_id, peer_id); });
      return;
    }

    size_t const target_highest = std::min(batch_index + syncWindowSize, limit - 1);
    std::vector<size_t> batches_to_send;
    for (size_t i = highest_sent + 1; i <= target_highest; ++i) {
      batches_to_send.push_back(i);
    }

    if (!batches_to_send.empty()) {
      LGI << "[Sync] Sending next batches " << batches_to_send.front() << " to " << batches_to_send.back()
          << " out of approx " << limit << " for peer " << peer_id.ToString();
    }

    SendSyncBatches(peer_id, topic_id, session_key, lca, my_total, batches_to_send);
  }

  void ProcessPendingSyncs() {
    if (!isRunning_) {
      return;
    }
    auto const reqs = storage_->GetSyncRequests();
    std::unordered_set<std::string> processed;
    for (auto const& req : reqs) {
      bool const is_self = (req.target_peer_id == my_id_);
      if (!is_self) {
        auto contact = storage_->GetContact(req.target_peer_id);
        if (!contact || contact->status != identity::ContactStatus::confirmed) {
          continue;
        }
      }

      for (auto const& [net_id, peer] : transport_->GetPeers()) {
        if (transport_->GetMasterId(net_id) == req.target_peer_id) {
          std::string const key = GetSessionKey(req.topic_id, net_id);
          if (processed.contains(key)) {
            continue;
          }

          bool is_active = false;
          {
            std::lock_guard<std::mutex> const lock(sessions_mutex_);
            if (auto it = sessions_.find(key); it != sessions_.end()) {
              is_active = it->second.active;
            }
          }
          if (is_active) {
            processed.insert(key);
            continue;
          }
          processed.insert(key);

          if (peer->data_channel_is_open && peer->session_cipher->IsReady() && peer->crcp_verified) {
            ProposeSync(req.topic_id, net_id);
          }
        }
      }
    }
  }
};

}  // namespace eillance::sync
