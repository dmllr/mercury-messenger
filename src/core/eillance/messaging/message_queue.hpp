#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/utils/string.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::messaging {

struct Message {
  std::string id;
  std::string peer_id;
  std::string content;
  Time::Unit timestamp;
};

// JSON serialization for Message struct
inline void to_json(nlohmann::json& j, const Message& m) { // NOLINT(*-identifier-naming)
    j = nlohmann::json{{"id", m.id}, {"peer_id", m.peer_id}, {"content", m.content}, {"timestamp", m.timestamp}};
}

inline void from_json(const nlohmann::json& j, Message& m) { // NOLINT(*-identifier-naming)
    j.at("id").get_to(m.id);
    j.at("peer_id").get_to(m.peer_id);
    j.at("content").get_to(m.content);
    j.at("timestamp").get_to(m.timestamp);
}


class MessageQueue {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<std::string, std::deque<Message>> queue_map_;
  mutable std::mutex mutex_;

  static std::string GetStorageKey(const std::string& peer_id) {
    return "messages_" + peer_id;
  }

  void LoadQueue(const std::string& peer_id) {
    // This function must be called from within a locked context
    if (queue_map_.contains(peer_id)) {
      return;  // Already loaded
    }

    auto key = GetStorageKey(peer_id);
    if (storage_->Exists(key)) {
      const auto data_opt = storage_->Load(key);
      if (data_opt) {
        try {
          std::string const json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
          const nlohmann::json j = nlohmann::json::parse(json_str);
          queue_map_[peer_id] = j.get<std::deque<Message>>();
        } catch (const nlohmann::json::parse_error& e) {
          LOGE << "Failed to parse message queue for peer " << peer_id << ": " << e.what();
          storage_->Delete(key);  // Corrupted data
        }
      }
    }
  }

  void SaveQueue(const std::string& peer_id) {
    // This function must be called from within a locked context
    const auto key = GetStorageKey(peer_id);
    const auto& queue = queue_map_[peer_id];

    if (queue.empty()) {
      if (storage_->Exists(key)) {
        storage_->Delete(key);
      }
      return;
    }

    const nlohmann::json j = queue;
    const std::string json_str = j.dump();
    const auto* char_ptr = reinterpret_cast<const std::byte*>(json_str.data());
    const std::vector<std::byte> data(char_ptr, char_ptr + json_str.size());
    if (!storage_->Save(key, data)) {
        LOGE << "Failed to save message queue for peer " << peer_id;
    }
  }

public:
  explicit MessageQueue(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
  }

  void Enqueue(const std::string& peer_id, const std::string& content) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadQueue(peer_id);
    auto& queue = queue_map_[peer_id];

    const Message msg{.id = StringUtils::RandomString(),
                .peer_id = peer_id,
                .content = content,
                .timestamp = Time::Now()};
    queue.push_back(msg);
    SaveQueue(peer_id);
  }

  std::vector<Message> GetQueuedMessages(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadQueue(peer_id);
    if (!queue_map_.contains(peer_id)) {
        return {};
    }
    auto& queue = queue_map_[peer_id];
    return {queue.begin(), queue.end()};
  }

  void Clear(const std::string& peer_id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (queue_map_.contains(peer_id)) {
      queue_map_[peer_id].clear();
    }
    SaveQueue(peer_id);
  }
};

}  // namespace eillance::messaging
