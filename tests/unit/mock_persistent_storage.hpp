#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <ranges>

#include "eillance/crypto/crypto.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::storage {

class MockPersistentStorageImpl : public PersistentStorageImpl {
  mutable std::recursive_mutex mutex_;
  std::unordered_map<std::string, std::string> settings_;
  std::unordered_map<ContactID, identity::Contact> contacts_;
  std::unordered_map<GroupID, identity::Group> groups_;
  std::unordered_map<ChannelID, identity::Channel> channels_;
  std::unordered_map<TopicID, std::vector<messaging::HistoryEntryRecord>> history_;
  std::unordered_map<uint64_t, std::string> large_messages_;
  std::vector<messaging::LedgerEntry> ledger_;
  std::vector<messaging::SyncRequest> sync_reqs_;
  std::atomic<uint64_t> next_history_id_{1};
  std::string db_path_ = "mock_db_path";

  static auto ToLower(std::string const& str) -> std::string {
    std::string lower = str;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower;
  }

 public:
  // Public counter for testing rate-limiting or call frequency
  std::atomic<int> unsuspend_call_count{0};

  explicit MockPersistentStorageImpl(std::string path = "mock_db_path")
      : db_path_(std::move(path)) {
  }

  // --- Path ---
  auto GetPath() const -> std::string override {
    return db_path_;
  }

  // --- Settings ---
  auto GetSetting(std::string_view const key) -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = settings_.find(std::string(key)); itr != settings_.end()) {
      return itr->second;
    }
    return std::nullopt;
  }

  void SetSetting(std::string_view const key, std::string_view const value) override {
    std::lock_guard const lock(mutex_);
    settings_[std::string(key)] = std::string(value);
  }

  void DeleteSetting(std::string_view const key) override {
    std::lock_guard const lock(mutex_);
    settings_.erase(std::string(key));
  }

  auto GetRaw(std::string const& key) -> std::optional<std::vector<std::byte>> override {
    if (auto const val = GetSetting(key)) {
      return crypto::FromHex(*val);
    }
    return std::nullopt;
  }

  auto SetRaw(std::string const& key, std::vector<std::byte> const& value) -> bool override {
    SetSetting(key, crypto::ToHex(value));
    return true;
  }

  // --- Contacts ---
  auto GetContacts() -> std::vector<identity::Contact> override {
    std::lock_guard const lock(mutex_);
    std::vector<identity::Contact> result;
    result.reserve(contacts_.size());
    for (auto const& contact : contacts_ | std::views::values) {
      result.push_back(contact);
    }
    return result;
  }

  auto GetContact(ContactID const& peer_id) -> std::optional<identity::Contact> override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = contacts_.find(peer_id); itr != contacts_.end()) {
      return itr->second;
    }
    return std::nullopt;
  }

  void UpdateContact(identity::Contact const& contact) override {
    std::lock_guard const lock(mutex_);
    contacts_[contact.peer_id] = contact;
  }

  void RemoveContact(ContactID const& peer_id) override {
    std::lock_guard const lock(mutex_);
    contacts_.erase(peer_id);

    if (auto it = history_.find(TopicID(peer_id)); it != history_.end()) {
      for (auto const& entry : it->second) {
        large_messages_.erase(entry.id);
      }
      history_.erase(it);
    }
  }

  // --- Groups ---
  void AddGroup(identity::Group const& group) override {
    std::lock_guard const lock(mutex_);
    groups_[group.id] = group;
  }
  auto GetGroup(GroupID const& group_id) -> std::optional<identity::Group> override {
    std::lock_guard const lock(mutex_);
    if (auto it = groups_.find(group_id); it != groups_.end()) return it->second;
    return std::nullopt;
  }
  auto GetGroups() -> std::vector<identity::Group> override {
    std::lock_guard const lock(mutex_);
    std::vector<identity::Group> res;
    for (auto const& [k, v] : groups_) res.push_back(v);
    return res;
  }
  void RemoveGroup(GroupID const& group_id) override {
    std::lock_guard const lock(mutex_);
    groups_.erase(group_id);
    history_.erase(TopicID(group_id));
  }

  // --- Channels ---
  void AddChannel(identity::Channel const& channel) override {
    std::lock_guard const lock(mutex_);
    channels_[channel.id] = channel;
  }
  auto GetChannel(ChannelID const& channel_id) -> std::optional<identity::Channel> override {
    std::lock_guard const lock(mutex_);
    if (auto it = channels_.find(channel_id); it != channels_.end()) return it->second;
    return std::nullopt;
  }
  auto GetChannels() -> std::vector<identity::Channel> override {
    std::lock_guard const lock(mutex_);
    std::vector<identity::Channel> res;
    for (auto const& [k, v] : channels_) res.push_back(v);
    return res;
  }
  void RemoveChannel(ChannelID const& channel_id) override {
    std::lock_guard const lock(mutex_);
    channels_.erase(channel_id);
    history_.erase(TopicID(channel_id));
  }

  // --- Lifecycle ---
  void Close() override {
  }
  auto Open() -> bool override {
    return true;
  }
  void Checkpoint() override {
  }

  // --- Synchronization Ledger ---
  void AddLedgerEntry(messaging::LedgerEntry const& entry) override {
    std::lock_guard const lock(mutex_);
    ledger_.push_back(entry);
  }

  auto GetLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::vector<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::LedgerEntry> res;
    bool found = hash.IsEmpty();
    for (auto const& e : ledger_) {
      if (e.topic_id == topic_id) {
        if (found) res.push_back(e);
        if (e.hash == hash) found = true;
      }
    }
    return res;
  }

  auto GetLedgerEntries(TopicID const& topic_id) -> std::vector<messaging::LedgerEntry> override {
    return GetLedgerEntriesAfter(topic_id, messaging::LedgerHash());
  }

  auto GetLatestLedgerEntry(TopicID const& topic_id) -> std::optional<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    for (auto it = ledger_.rbegin(); it != ledger_.rend(); ++it) {
      if (it->topic_id == topic_id) return *it;
    }
    return std::nullopt;
  }

  void DeleteLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash) override {
    std::lock_guard const lock(mutex_);
    if (hash.IsEmpty()) {
      std::erase_if(ledger_, [&](auto& e) { return e.topic_id == topic_id; });
      return;
    }
    bool found = false;
    std::erase_if(ledger_, [&](auto& e) {
      if (e.topic_id == topic_id) {
        if (found) return true;
        if (e.hash == hash) found = true;
      }
      return false;
    });
  }

  auto GetLedgerEntryByHash(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::optional<messaging::LedgerEntry> override {
    std::lock_guard const lock(mutex_);
    for (auto const& e : ledger_) {
      if (e.topic_id == topic_id && e.hash == hash) return e;
    }
    return std::nullopt;
  }

  auto GetRecentLedgerHashes(TopicID const& topic_id) -> std::vector<messaging::LedgerHash> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::LedgerHash> res;
    for (auto it = ledger_.rbegin(); it != ledger_.rend(); ++it) {
      if (it->topic_id == topic_id) res.push_back(it->hash);
    }
    if (res.empty()) return {messaging::LedgerHash()};
    return res;
  }

  auto HasLedgerEntry(TopicID const& topic_id, messaging::LedgerHash const& hash) -> bool override {
    if (hash.IsEmpty()) return true;
    std::lock_guard const lock(mutex_);
    for (auto const& e : ledger_) {
      if (e.topic_id == topic_id && e.hash == hash) return true;
    }
    return false;
  }

  void ReplaceLedgerEntries(
      TopicID const& topic_id,
      messaging::LedgerHash const& lca_hash,
      std::vector<messaging::LedgerEntry> const& entries
  ) override {
    DeleteLedgerEntriesAfter(topic_id, lca_hash);
    std::lock_guard const lock(mutex_);
    for (auto const& e : entries) {
      bool exists = false;
      for (auto const& existing : ledger_) {
        if (existing.topic_id == e.topic_id && existing.hash == e.hash) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        ledger_.push_back(e);
      }
    }
  }

  // --- Sync Requests ---
  void AddSyncRequest(messaging::SyncRequest const& req) override {
    std::lock_guard const lock(mutex_);
    sync_reqs_.push_back(req);
  }

  auto GetSyncRequests() -> std::vector<messaging::SyncRequest> override {
    std::lock_guard const lock(mutex_);
    return sync_reqs_;
  }

  void RemoveSyncRequest(TopicID const& topic_id, ContactID const& target_peer_id) override {
    std::lock_guard const lock(mutex_);
    std::erase_if(sync_reqs_, [&](auto& r) { return r.topic_id == topic_id && r.target_peer_id == target_peer_id; });
  }

  void
  UpdateSyncRequestAttempt(TopicID const& topic_id, ContactID const& target_peer_id, Timestamp attempt_time) override {
    std::lock_guard const lock(mutex_);
    for (auto& r : sync_reqs_) {
      if (r.topic_id == topic_id && r.target_peer_id == target_peer_id) r.last_attempt = attempt_time;
    }
  }

  // --- History ---
  void ClearTopicReadModel(TopicID const& topic_id) override {
    std::lock_guard const lock(mutex_);
    history_.erase(topic_id);
  }

  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    std::lock_guard const lock(mutex_);
    if (entry.id == 0) {
      entry.id = next_history_id_++;
    } else {
      next_history_id_ = std::max(next_history_id_.load(), entry.id + 1);
    }

    constexpr size_t k_max_inline_content_size = 2048;
    if (auto* content = messaging::GetMessageContent(entry.data)) {
      if (content->content.length() > k_max_inline_content_size) {
        large_messages_[entry.id] = content->content;
        content->content = content->content.substr(0, k_max_inline_content_size);
        entry.is_truncated = true;
      } else {
        entry.is_truncated = false;
      }
    }

    history_[entry.topic_id].push_back(entry);
    return entry;
  }

  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) override {
    std::lock_guard const lock(mutex_);
    for (auto& history_vec : history_ | std::views::values) {
      for (auto& existing_entry : history_vec) {
        if (existing_entry.id == entry.id) {
          existing_entry = entry;
          return;
        }
      }
    }
  }

  void UpdateMessageStatus(TopicID const& topic_id, uint64_t message_id, messaging::MessageStatus new_status) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (entry.id == message_id) {
          entry.status = new_status;
          return;
        }
      }
    }
  }

  void UpdateMessageStatuses(
      TopicID const& topic_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (std::ranges::find(message_ids, entry.id) != message_ids.end()) {
          entry.status = new_status;
        }
      }
    }
  }

  void UpdateMessagePeerStatus(
      TopicID const& topic_id,
      uint64_t message_id,
      ContactID const& peer_id,
      messaging::MessageStatus new_status
  ) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (entry.id == message_id) {
          if (auto* group_msg = std::get_if<messaging::GroupMessageEntry>(&entry.data)) {
            bool found = false;
            for (auto& ps : group_msg->peer_statuses) {
              if (ps.peer_id == peer_id) {
                ps.status = new_status;
                found = true;
                break;
              }
            }
            if (!found) {
              group_msg->peer_statuses.push_back({peer_id, new_status});
            }
          }
          return;
        }
      }
    }
  }

  void UpdateMessageContent(TopicID const& topic_id, uint64_t message_id, std::string const& new_content) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (entry.id == message_id) {
          if (auto* content = messaging::GetMessageContent(entry.data)) {
            content->content = new_content;
            entry.is_edited = true;
          }
          return;
        }
      }
    }
  }

  void MarkMessageDeleted(TopicID const& topic_id, uint64_t message_id) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (entry.id == message_id) {
          entry.is_deleted = true;
          if (auto* content = messaging::GetMessageContent(entry.data)) {
            content->content = "";
            content->attachments.clear();
          }
          return;
        }
      }
    }
  }

  void ToggleReaction(
      TopicID const& topic_id,
      uint64_t message_id,
      ContactID const& author_id,
      messaging::Reaction reaction
  ) override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto& entry : itr->second) {
        if (entry.id == message_id) {
          auto& reactions = entry.reactions;
          auto it = std::ranges::find_if(reactions, [&](auto const& r) {
            return r.author_id == author_id && r.reaction == reaction;
          });
          if (it != reactions.end()) {
            reactions.erase(it);
          } else {
            reactions.push_back({author_id, reaction});
          }
          return;
        }
      }
    }
  }

  void UpdateAttachmentStatus(
      std::string const& attachment_id,
      messaging::AttachmentStatus status,
      bool is_downloaded
  ) override {
    std::lock_guard const lock(mutex_);
    for (auto& history_vec : history_ | std::views::values) {
      for (auto& entry : history_vec) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto& att : content->attachments) {
            if (att.id == attachment_id) {
              att.status = status;
              att.is_downloaded = is_downloaded;
              return;
            }
          }
        }
      }
    }
  }

  auto GetAttachment(std::string const& attachment_id) -> std::optional<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    for (auto& history_vec : history_ | std::views::values) {
      for (auto& entry : history_vec) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto& att : content->attachments) {
            if (att.id == attachment_id) {
              return att;
            }
          }
        }
      }
    }
    return std::nullopt;
  }

  auto GetDownloadedAttachments(TopicID const& topic_id) -> std::vector<messaging::AttachmentRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::AttachmentRecord> results;
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto const& att : content->attachments) {
            if (att.is_downloaded && att.status == messaging::AttachmentStatus::done) {
              results.push_back({att, entry.timestamp});
            }
          }
        }
      }
    }
    std::ranges::sort(results, [](auto const& left, auto const& right) { return left.timestamp > right.timestamp; });
    return results;
  }

  auto GetAllAttachments(TopicID const& topic_id) -> std::vector<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::Attachment> attachments;
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto const& att : content->attachments) {
            attachments.push_back(att);
          }
        }
      }
    }
    return attachments;
  }

  auto GetHistoryPage(TopicID const& topic_id, size_t page_index, size_t page_size) -> messaging::HistoryPage override {
    std::lock_guard const lock(mutex_);
    messaging::HistoryPage page;
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      auto peer_history_copy = itr->second;  // Copy to sort
      page.total_messages = peer_history_copy.size();

      // Sort by timestamp descending to simulate DB query
      std::ranges::sort(peer_history_copy, [](auto const& left, auto const& right) {
        return left.timestamp > right.timestamp;
      });

      size_t const start = page_index * page_size;
      if (start < peer_history_copy.size()) {
        size_t const end = std::min(start + page_size, peer_history_copy.size());
        page.messages.assign(peer_history_copy.begin() + start, peer_history_copy.begin() + end);
      }

      // Reverse to get ascending time order for display, matching SQLiteStorage behavior
      std::ranges::reverse(page.messages);
    } else {
      page.total_messages = 0;
    }
    return page;
  }

  auto GetHistoryEntry(TopicID const& topic_id, uint64_t const message_id)
      -> std::optional<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (entry.id == message_id) {
          return entry;
        }
      }
    }
    return std::nullopt;
  }

  auto GetFullMessageContent(TopicID const& /*topic_id*/, std::uint64_t const message_id)
      -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    if (auto const itr = large_messages_.find(message_id); itr != large_messages_.end()) {
      return itr->second;
    }
    return std::nullopt;
  }

  auto GetChatSummaries(std::vector<TopicID> const& topic_ids) -> std::vector<messaging::ChatSummary> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (auto const& topic_id : topic_ids) {
      if (auto const itr = history_.find(topic_id); itr != history_.end() && !itr->second.empty()) {
        auto& peer_history = itr->second;
        // Find the last message (most recent timestamp)
        auto last_message_it = std::ranges::max_element(peer_history, [](auto const& left, auto const& right) {
          return left.timestamp < right.timestamp;
        });

        uint32_t unread_count = 0;
        for (auto const& entry : peer_history) {
          if (entry.kind == messaging::HistoryEntryKind::textMessage &&
              entry.direction == messaging::MessageDirection::received &&
              entry.status != messaging::MessageStatus::read) {
            unread_count++;
          }
        }
        summaries.push_back({topic_id, *last_message_it, unread_count});
      }
    }

    // Sort summaries by timestamp of the last message, descending
    std::ranges::sort(summaries, [](auto const& left, auto const& right) {
      return left.last_message.timestamp > right.last_message.timestamp;
    });

    return summaries;
  }

  auto GetMessagesWithStatus(TopicID const& topic_id, messaging::MessageStatus const status)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> records;
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (entry.status == status) {
          records.push_back(entry);
        }
      }
    }
    return records;
  }

  auto GetPendingAttachments(TopicID const& topic_id) -> std::vector<messaging::Attachment> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::Attachment> attachments;
    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto const& att : content->attachments) {
            if ((att.status == messaging::AttachmentStatus::pending ||
                 att.status == messaging::AttachmentStatus::transferring) &&
                !att.is_downloaded) {
              attachments.push_back(att);
            }
          }
        }
      }
    }
    return attachments;
  }

  // --- Search ---
  auto SearchMessages(TopicID const& topic_id, std::string const& query)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> results;
    std::string const query_lower = ToLower(query);

    if (auto const itr = history_.find(topic_id); itr != history_.end()) {
      for (auto const& entry : itr->second) {
        if (entry.kind != messaging::HistoryEntryKind::textMessage &&
            entry.kind != messaging::HistoryEntryKind::systemMessage) {
          continue;
        }
        std::string content;
        if (entry.is_truncated) {
          if (auto const full = large_messages_.find(entry.id); full != large_messages_.end()) {
            content = full->second;
          } else if (auto* c = messaging::GetMessageContent(entry.data)) {
            content = c->content;
          }
        } else if (auto* c = messaging::GetMessageContent(entry.data)) {
          content = c->content;
        }

        if (ToLower(content).find(query_lower) != std::string::npos) {
          auto record = entry;
          // Ensure returned record has full content for display context
          if (entry.is_truncated) {
            if (auto* c = messaging::GetMessageContent(record.data)) {
              c->content = content;
            }
          }
          results.push_back(record);
        }
      }
    }
    // Sort descending by timestamp
    std::ranges::sort(results, [](auto const& left, auto const& right) { return left.timestamp > right.timestamp; });
    return results;
  }

  auto SearchAllMessages(std::string const& query) -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> results;
    for (auto const& topic_id : history_ | std::views::keys) {
      auto topic_results = SearchMessages(topic_id, query);
      results.insert(results.end(), topic_results.begin(), topic_results.end());
    }
    // Global sort descending
    std::ranges::sort(results, [](auto const& left, auto const& right) { return left.timestamp > right.timestamp; });
    return results;
  }
};

}  // namespace eillance::storage
