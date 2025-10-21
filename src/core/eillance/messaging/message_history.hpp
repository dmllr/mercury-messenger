#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <algorithm>
#include <string_view>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

enum class MessageDirection {
  Sent,
  Received
};

// Helper for JSON serialization
NLOHMANN_JSON_SERIALIZE_ENUM(MessageDirection, { // NOLINT
                                                   {MessageDirection::Sent, "sent"},
                                                   {MessageDirection::Received, "received"},
                                               })

struct HistoryMessage {
  std::string content;
  Time::Unit timestamp;
  MessageDirection direction;
};

// JSON serialization for HistoryMessage struct (MUST be defined before it's used by other types)
inline void to_json(nlohmann::json& j, const HistoryMessage& m) {
  j = nlohmann::json{
      {"content", m.content},
      {"timestamp", m.timestamp},
      {"direction", m.direction}};
}

inline void from_json(const nlohmann::json& j, HistoryMessage& m) {
  j.at("content").get_to(m.content);
  j.at("timestamp").get_to(m.timestamp);
  j.at("direction").get_to(m.direction);
}

struct ChatSummary {
    std::string peer_id;
    HistoryMessage last_message;
    uint32_t unread_count = 0;
};

inline void to_json(nlohmann::json& j, const ChatSummary& s) {
    j["peer_id"] = s.peer_id;
    j["last_message"] = s.last_message;
    j["unread_count"] = s.unread_count;
}

struct HistoryPage {
    std::vector<HistoryMessage> messages;
    size_t total_messages;
};

// Provide serialization for the deque of HistoryMessage
inline void to_json(nlohmann::json& j, const std::deque<HistoryMessage>& deq) {
    j = nlohmann::json::array();
    for(const auto& msg : deq) {
        j.push_back(msg);
    }
}

inline void from_json(const nlohmann::json& j, std::deque<HistoryMessage>& deq) {
    for (const auto& item : j) {
        deq.push_back(item.get<HistoryMessage>());
    }
}


class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;
  // Cache for loaded history to avoid frequent disk access.
  std::unordered_map<std::string, std::deque<HistoryMessage>> history_cache_;
  mutable std::mutex mutex_;

  static constexpr std::string GetStorageKey(std::string_view peer_id) {
    return std::string("history_") + std::string(peer_id);
  }

  void LoadHistory(const std::string& peer_id) {
    // Must be called from a locked context.
    if (history_cache_.contains(peer_id)) {
      return;  // Already loaded.
    }

    auto key = GetStorageKey(peer_id);
    if (storage_->Exists(key)) {
      const auto data_opt = storage_->Load(key);
      if (data_opt) {
        try {
          const std::string json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
          const nlohmann::json j = nlohmann::json::parse(json_str);
          history_cache_[peer_id] = j.get<std::deque<HistoryMessage>>();
        } catch (const nlohmann::json::parse_error& e) {
          LOGE << "Failed to parse message history for peer " << peer_id << ": " << e.what();
          storage_->Delete(key);  // Corrupted data.
        }
      }
    }
  }

  void SaveHistory(const std::string& peer_id) {
    // Must be called from a locked context.
    const auto key = GetStorageKey(peer_id);
    const auto& history = history_cache_[peer_id];

    if (history.empty()) {
      if (storage_->Exists(key)) {
        storage_->Delete(key);
      }
      return;
    }

    const nlohmann::json j = history;
    const std::string json_str = j.dump();
    const auto* char_ptr = reinterpret_cast<const std::byte*>(json_str.data());
    const std::vector<std::byte> data(char_ptr, char_ptr + json_str.size());
    if (!storage_->Save(key, data)) {
        LOGE << "Failed to save message history for peer " << peer_id;
    }
  }

public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {}

  HistoryMessage AddMessage(const std::string& peer_id, const std::string& content, MessageDirection direction) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory(peer_id);
    auto& history = history_cache_[peer_id];

    const HistoryMessage msg{.content = content,
                             .timestamp = Time::Now(),
                             .direction = direction};

    history.push_back(msg);
    SaveHistory(peer_id);
    return msg;
  }

  HistoryPage GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory(peer_id);

    if (!history_cache_.contains(peer_id)) {
        return {{}, 0};
    }

    const auto& history = history_cache_.at(peer_id);
    const size_t total_size = history.size();

    // Pages are indexed from 0, where page 0 is the most recent.
    // Calculate the start index for the requested page from the end of the deque.
    const size_t end_offset = page_index * page_size;
    if (end_offset >= total_size) {
        return {{}, total_size}; // Page is out of bounds
    }

    const size_t start_offset = std::min(total_size, (page_index + 1) * page_size);

    const auto start_it = history.end() - start_offset;
    const auto end_it = history.end() - end_offset;

    std::vector<HistoryMessage> messages_page(start_it, end_it);
    return {std::move(messages_page), total_size};
  }

  std::vector<ChatSummary> GetChatSummaries(const std::vector<std::string>& peer_ids) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChatSummary> summaries;

    for (const auto& peer_id : peer_ids) {
        LoadHistory(peer_id);
        if (history_cache_.contains(peer_id) && !history_cache_.at(peer_id).empty()) {
            summaries.push_back({
                .peer_id = peer_id,
                .last_message = history_cache_.at(peer_id).back(),
                .unread_count = 0 // Unread count logic to be implemented
            });
        }
    }

    // Sort by timestamp of the last message, descending
    std::ranges::sort(summaries, [](const auto& a, const auto& b) {
        return a.last_message.timestamp > b.last_message.timestamp;
    });

    return summaries;
  }
};

}  // namespace eillance::messaging
