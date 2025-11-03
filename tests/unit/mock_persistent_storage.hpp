#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <ranges>

#include "eillance/storage/persistent_storage.hpp"
#include "eillance/crypto/crypto.hpp"

namespace eillance::storage {

class MockPersistentStorageImpl final : public PersistentStorageImpl {
  mutable std::recursive_mutex mutex_;
  std::unordered_map<std::string, std::string> settings_;
  std::unordered_map<std::string, identity::Contact> contacts_;
  std::unordered_map<std::string, std::vector<messaging::HistoryEntryRecord>> history_;
  std::unordered_map<uint64_t, std::string> large_messages_;
  std::atomic<uint64_t> next_history_id_{1};

public:
  // --- Settings ---
  auto GetSetting(std::string_view key) -> std::optional<std::string> override {
    const std::lock_guard lock(mutex_);
    if (auto it = settings_.find(std::string(key)); it != settings_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void SetSetting(std::string_view key, std::string_view value) override {
    const std::lock_guard lock(mutex_);
    settings_[std::string(key)] = std::string(value);
  }

  void DeleteSetting(std::string_view key) override {
    const std::lock_guard lock(mutex_);
    settings_.erase(std::string(key));
  }

  auto GetRaw(const std::string& key) -> std::optional<std::vector<std::byte>> override {
      if (auto val = GetSetting(key)) { return crypto::FromHex(*val); }
      return std::nullopt;
  }

  auto SetRaw(const std::string& key, const std::vector<std::byte>& value) -> bool override {
      SetSetting(key, crypto::ToHex(value));
      return true;
  }

  // --- Contacts ---
  auto GetContacts() -> std::vector<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    std::vector<identity::Contact> result;
    for (const auto& pair : contacts_) {
      result.push_back(pair.second);
    }
    return result;
  }

  auto GetContact(std::string_view peer_id) -> std::optional<identity::Contact> override {
    const std::lock_guard lock(mutex_);
    if (auto it = contacts_.find(std::string(peer_id)); it != contacts_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  void UpdateContact(const identity::Contact& contact) override {
    const std::lock_guard lock(mutex_);
    contacts_[contact.peer_id] = contact;
  }

  void RemoveContact(std::string_view peer_id) override {
    const std::lock_guard lock(mutex_);
    contacts_.erase(std::string(peer_id));
  }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord override {
    const std::lock_guard lock(mutex_);
    entry.id = next_history_id_++;

    constexpr size_t kMaxInlineContentSize = 2048;
    if (auto* msg_entry = std::get_if<messaging::MessageEntry>(&entry.data)) {
        if (msg_entry->content.length() > kMaxInlineContentSize) {
            large_messages_[entry.id] = msg_entry->content;
            msg_entry->content = msg_entry->content.substr(0, kMaxInlineContentSize);
            entry.is_truncated = true;
        } else {
            entry.is_truncated = false;
        }
    }

    history_[entry.peer_id_for_db].push_back(entry);
    return entry;
  }

  void UpdateHistoryEntry(const messaging::HistoryEntryRecord& entry) override {
    const std::lock_guard lock(mutex_);
    for (auto& [peer_id, history_vec] : history_) {
        for (auto& existing_entry : history_vec) {
            if (existing_entry.id == entry.id) {
                existing_entry = entry;
                return;
            }
        }
    }
  }

  void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) override {
    const std::lock_guard lock(mutex_);
    if (auto it = history_.find(peer_id); it != history_.end()) {
      for (auto& entry : it->second) {
        if (entry.id == message_id) {
          entry.status = new_status;
          return;
        }
      }
    }
  }

  void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) override {
      const std::lock_guard lock(mutex_);
      if (auto it = history_.find(peer_id); it != history_.end()) {
          for (auto& entry : it->second) {
              if (std::find(message_ids.begin(), message_ids.end(), entry.id) != message_ids.end()) {
                  entry.status = new_status;
              }
          }
      }
  }


  auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) -> messaging::HistoryPage override {
    const std::lock_guard lock(mutex_);
    messaging::HistoryPage page;
    if (auto it = history_.find(peer_id); it != history_.end()) {
      auto peer_history_copy = it->second; // Copy to sort
      page.total_messages = peer_history_copy.size();

      // Sort by timestamp descending to simulate DB query
      std::sort(peer_history_copy.begin(), peer_history_copy.end(), [](const auto& a, const auto& b) {
        return a.timestamp > b.timestamp;
      });

      size_t start = page_index * page_size;
      if (start < peer_history_copy.size()) {
        size_t end = std::min(start + page_size, peer_history_copy.size());
        page.messages.assign(peer_history_copy.begin() + start, peer_history_copy.begin() + end);
      }

      // Reverse to get ascending time order for display, matching SQLiteStorage behavior
      std::ranges::reverse(page.messages);
    } else {
        page.total_messages = 0;
    }
    return page;
  }

  auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> override {
    const std::lock_guard lock(mutex_);
    if (auto it = large_messages_.find(message_id); it != large_messages_.end()) {
        return it->second;
    }
    return std::nullopt;
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<messaging::ChatSummary> override {
    const std::lock_guard lock(mutex_);
    std::vector<messaging::ChatSummary> summaries;

    for (const auto& peer_id : peer_ids) {
      if (auto it = history_.find(peer_id); it != history_.end() && !it->second.empty()) {
        auto& peer_history = it->second;
        // Find the last message (most recent timestamp)
        auto last_message_it = std::max_element(peer_history.begin(), peer_history.end(),
            [](const auto& a, const auto& b) {
            return a.timestamp < b.timestamp;
        });

        uint32_t unread_count = 0;
        for (const auto& entry : peer_history) {
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
    std::sort(summaries.begin(), summaries.end(), [](const auto& a, const auto& b) {
        return a.last_message.timestamp > b.last_message.timestamp;
    });

    return summaries;
  }

  auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) -> std::vector<messaging::HistoryEntryRecord> override {
    const std::lock_guard lock(mutex_);
    std::vector<messaging::HistoryEntryRecord> records;
     if (auto it = history_.find(peer_id); it != history_.end()) {
         for(const auto& entry : it->second) {
             if (entry.status == status) {
                 records.push_back(entry);
             }
         }
     }
    return records;
  }
};

}  // namespace eillance::storage
