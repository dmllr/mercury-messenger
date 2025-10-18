#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

// JSON serialization for HistoryMessage struct
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


class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;
  // Cache for loaded history to avoid frequent disk access.
  std::unordered_map<std::string, std::deque<HistoryMessage>> history_cache_;
  mutable std::mutex mutex_;

  static std::string GetStorageKey(const std::string& peer_id) {
    return "history_" + peer_id;
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
    storage_->Save(key, data);
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

  std::deque<HistoryMessage> GetHistory(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory(peer_id);
    if (!history_cache_.contains(peer_id)) {
      return {};
    }
    return history_cache_.at(peer_id);
  }
};

}  // namespace eillance::messaging
