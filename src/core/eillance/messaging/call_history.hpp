#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

enum class CallDirection { Outgoing, Incoming };
enum class CallStatus { Answered, Missed, Rejected, Busy };

NLOHMANN_JSON_SERIALIZE_ENUM(CallDirection, { // NOLINT
    {CallDirection::Outgoing, "outgoing"},
    {CallDirection::Incoming, "incoming"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(CallStatus, { // NOLINT
    {CallStatus::Answered, "answered"},
    {CallStatus::Missed, "missed"},
    {CallStatus::Rejected, "rejected"},
    {CallStatus::Busy, "busy"},
})

struct CallRecord {
    std::string peer_id;
    Time::Unit timestamp;
    uint32_t duration_seconds = 0;
    CallDirection direction;
    CallStatus status;
};

inline void to_json(nlohmann::json& j, const CallRecord& r) {
  j["peer_id"] = r.peer_id;
  j["timestamp"] = r.timestamp;
  j["duration_seconds"] = r.duration_seconds;
  j["direction"] = r.direction;
  j["status"] = r.status;
}

inline void from_json(const nlohmann::json& j, CallRecord& r) {
  j.at("peer_id").get_to(r.peer_id);
  j.at("timestamp").get_to(r.timestamp);
  j.at("duration_seconds").get_to(r.duration_seconds);
  j.at("direction").get_to(r.direction);
  j.at("status").get_to(r.status);
}

class CallHistory {
  static constexpr auto kHistoryFile = "call_history.json";
  static constexpr size_t kMaxHistorySize = 200;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::deque<CallRecord> history_cache_;
  mutable std::mutex mutex_;
  bool is_loaded_ = false;

  void LoadHistory() {
    // Must be called from a locked context.
    if (is_loaded_) return;

    if (storage_->Exists(kHistoryFile)) {
        const auto data_opt = storage_->Load(kHistoryFile);
        if (data_opt) {
            try {
                const std::string json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
                history_cache_ = nlohmann::json::parse(json_str).get<std::deque<CallRecord>>();
            } catch (const nlohmann::json::parse_error& e) {
                LOGE << "Failed to parse call history: " << e.what();
                storage_->Delete(kHistoryFile);
            }
        }
    }
    is_loaded_ = true;
  }

  void SaveHistory() {
    // Must be called from a locked context.
    const nlohmann::json j = history_cache_;
    const std::string json_str = j.dump();
    const std::vector<std::byte> data(reinterpret_cast<const std::byte*>(json_str.data()), reinterpret_cast<const std::byte*>(json_str.data()) + json_str.size());
    if (!storage_->Save(kHistoryFile, data)) {
      LOGE << "Failed to save call history.";
    }
  }

public:
  explicit CallHistory(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) {}

  void AddRecord(CallRecord record) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory();
    history_cache_.push_front(std::move(record));
    if (history_cache_.size() > kMaxHistorySize) {
        history_cache_.pop_back();
    }
    SaveHistory();
  }

  std::vector<CallRecord> GetHistory() {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory();
    return {history_cache_.begin(), history_cache_.end()};
  }
};

} // namespace eillance::messaging
