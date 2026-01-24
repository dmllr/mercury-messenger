#pragma once

#include <algorithm>
#include <atomic>
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
  std::unordered_map<ContactID, std::vector<messaging::HistoryEntryRecord>> history_;
  std::unordered_map<uint64_t, std::string> large_messages_;
  std::unordered_map<ContactID, std::vector<uint64_t>> pending_read_receipts_;
  std::atomic<uint64_t> next_history_id_{1};
  std::string db_path_ = "mock_db_path";

 public:
  explicit MockPersistentStorageImpl(std::string path = "mock_db_path")
      : db_path_(std::move(path)) {}

  // --- Path ---
  auto GetPath() const -> std::string override { return db_path_; }

  // --- Settings ---
  auto GetSetting(std::string_view key) -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    if (auto it = settings_.find(std::string(key)); it != settings_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void SetSetting(std::string_view key, std::string_view value) override {
    std::lock_guard const lock(mutex_);
    settings_[std::string(key)] = std::string(value);
  }

  void DeleteSetting(std::string_view key) override {
    std::lock_guard const lock(mutex_);
    settings_.erase(std::string(key));
  }

  auto GetRaw(std::string const& key) -> std::optional<std::vector<std::byte>> override {
    if (auto val = GetSetting(key)) {
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
    for (auto const& pair : contacts_) {
      result.push_back(pair.second);
    }
    return result;
  }

  auto GetContact(ContactID const& peer_id) -> std::optional<identity::Contact> override {
    std::lock_guard const lock(mutex_);
    if (auto it = contacts_.find(peer_id); it != contacts_.end()) {
      return it->second;
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
  }

  void SetContactProfileSyncNeeded(ContactID const& peer_id, bool needed) override {
    std::lock_guard const lock(mutex_);
    if (auto it = contacts_.find(peer_id); it != contacts_.end()) {
      it->second.profile_sync_needed = needed;
    }
  }

  // --- Resilience ---
  void AddPendingReadReceipts(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) override {
    std::lock_guard const lock(mutex_);
    auto& pending = pending_read_receipts_[peer_id];
    for (auto id : message_ids) {
      if (std::find(pending.begin(), pending.end(), id) == pending.end()) {
        pending.push_back(id);
      }
    }
  }

  auto GetPendingReadReceipts(ContactID const& peer_id) -> std::vector<uint64_t> override {
    std::lock_guard const lock(mutex_);
    if (pending_read_receipts_.contains(peer_id)) {
      return pending_read_receipts_[peer_id];
    }
    return {};
  }

  void RemovePendingReadReceipts(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) override {
    std::lock_guard const lock(mutex_);
    if (pending_read_receipts_.contains(peer_id)) {
      auto& pending = pending_read_receipts_[peer_id];
      for (auto id : message_ids) {
        std::erase(pending, id);
      }
    }
  }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    std::lock_guard const lock(mutex_);
    entry.id = next_history_id_++;

    constexpr size_t k_max_inline_content_size = 2048;
    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
      if (msg_entry->content.length() > k_max_inline_content_size) {
        large_messages_[entry.id] = msg_entry->content;
        msg_entry->content = msg_entry->content.substr(0, k_max_inline_content_size);
        entry.is_truncated = true;
      } else {
        entry.is_truncated = false;
      }
    }

    history_[entry.peer_id].push_back(entry);
    return entry;
  }

  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) override {
    std::lock_guard const lock(mutex_);
    for (auto& [peer_id, history_vec] : history_) {
      for (auto& existing_entry : history_vec) {
        if (existing_entry.id == entry.id) {
          existing_entry = entry;
          return;
        }
      }
    }
  }

  void UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status)
      override {
    std::lock_guard const lock(mutex_);
    if (auto it = history_.find(peer_id); it != history_.end()) {
      for (auto& entry : it->second) {
        if (entry.id == message_id) {
          entry.status = new_status;
          return;
        }
      }
    }
  }

  void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) override {
    std::lock_guard const lock(mutex_);
    if (auto it = history_.find(peer_id); it != history_.end()) {
      for (auto& entry : it->second) {
        if (std::find(message_ids.begin(), message_ids.end(), entry.id) != message_ids.end()) {
          entry.status = new_status;
        }
      }
    }
  }

  auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage override {
    std::lock_guard const lock(mutex_);
    messaging::HistoryPage page;
    if (auto it = history_.find(peer_id); it != history_.end()) {
      auto peer_history_copy = it->second;  // Copy to sort
      page.total_messages = peer_history_copy.size();

      // Sort by timestamp descending to simulate DB query
      std::sort(peer_history_copy.begin(), peer_history_copy.end(), [](auto const& a, auto const& b) {
        return a.timestamp > b.timestamp;
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

  auto GetFullMessageContent(ContactID const& /*peer_id*/, std::uint64_t message_id)
      -> std::optional<std::string> override {
    std::lock_guard const lock(mutex_);
    if (auto it = large_messages_.find(message_id); it != large_messages_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (auto const& peer_id : peer_ids) {
      if (auto it = history_.find(peer_id); it != history_.end() && !it->second.empty()) {
        auto& peer_history = it->second;
        // Find the last message (most recent timestamp)
        auto last_message_it =
            std::max_element(peer_history.begin(), peer_history.end(), [](auto const& a, auto const& b) {
              return a.timestamp < b.timestamp;
            });

        uint32_t unread_count = 0;
        for (auto const& entry : peer_history) {
          if (entry.kind == messaging::HistoryEntryKind::textMessage &&
              entry.direction == messaging::MessageDirection::received &&
              entry.status != messaging::MessageStatus::read) {
            unread_count++;
          }
        }
        summaries.push_back({peer_id, *last_message_it, unread_count});
      }
    }

    // Sort summaries by timestamp of the last message, descending
    std::sort(summaries.begin(), summaries.end(), [](auto const& a, auto const& b) {
      return a.last_message.timestamp > b.last_message.timestamp;
    });

    return summaries;
  }

  auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> override {
    std::lock_guard const lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> records;
    if (auto it = history_.find(peer_id); it != history_.end()) {
      for (auto const& entry : it->second) {
        if (entry.status == status) {
          records.push_back(entry);
        }
      }
    }
    return records;
  }

  auto GetRemoteIds(ContactID const& peer_id, std::vector<uint64_t> const& local_ids)
      -> std::vector<uint64_t> override {
    std::lock_guard const lock(mutex_);
    std::vector<uint64_t> remote_ids;
    if (auto it = history_.find(peer_id); it != history_.end()) {
      for (auto const& entry : it->second) {
        if (std::find(local_ids.begin(), local_ids.end(), entry.id) != local_ids.end()) {
          if (entry.remote_id) {
            remote_ids.push_back(*entry.remote_id);
          }
        }
      }
    }
    return remote_ids;
  }

  // --- Debug ---
  void DebugClearAllHistory() override {
    std::lock_guard const lock(mutex_);
    history_.clear();
    large_messages_.clear();
    pending_read_receipts_.clear();
  }

  void DebugClearAllContacts() override {
    std::lock_guard const lock(mutex_);
    contacts_.clear();
  }

  void DebugRevokeAllContactApprovals() override {
    std::lock_guard const lock(mutex_);
    for (auto& [id, contact] : contacts_) {
      if (contact.status == identity::ContactStatus::confirmed) {
        contact.status = identity::ContactStatus::pendingApproval;
      }
    }
  }
};

}  // namespace eillance::storage
