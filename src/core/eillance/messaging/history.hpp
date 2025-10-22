#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <variant>
#include <string_view>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/string.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

enum class MessageDirection : std::uint8_t {
  sent,
  received
};

NLOHMANN_JSON_SERIALIZE_ENUM(MessageDirection, { // NOLINT
    {MessageDirection::sent, "sent"},
    {MessageDirection::received, "received"},
})

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

enum class MessageStatus : std::uint8_t {
    None, // Not applicable, or for received messages
    Sending,
    Sent,
    Delivered,
    Read
};

NLOHMANN_JSON_SERIALIZE_ENUM(MessageStatus, { // NOLINT
    {MessageStatus::None, "none"}, {MessageStatus::Sending, "sending"}, {MessageStatus::Sent, "sent"}, {MessageStatus::Delivered, "delivered"}, {MessageStatus::Read, "read"},
})

enum class HistoryEntryKind {
    TextMessage,
    VoiceCall,
    VideoCall,
    SystemMessage
};

NLOHMANN_JSON_SERIALIZE_ENUM(HistoryEntryKind, { // NOLINT
    {HistoryEntryKind::TextMessage, "text_message"},
    {HistoryEntryKind::VoiceCall, "voice_call"},
    {HistoryEntryKind::VideoCall, "video_call"},
    {HistoryEntryKind::SystemMessage, "system_message"},
})

// Data specific to a text or system message.
struct MessageEntry {
    std::string content;
};

// Data specific to a voice or video call record.
struct CallEntry {
    uint32_t duration_seconds = 0;
    CallDirection call_direction;
    CallStatus call_status;
};

// A variant to hold the data for any kind of history entry.
using HistoryEntryData = std::variant<MessageEntry, CallEntry>;

struct HistoryEntryRecord {
  std::uint64_t id;
  HistoryEntryKind kind;
  Time::Unit timestamp;
  MessageDirection direction;
  MessageStatus status = MessageStatus::None;
  HistoryEntryData data;
};


inline void to_json(nlohmann::json& j, const HistoryEntryRecord& e) { // NOLINT(*-swappable-parameters)
  j["id"] = e.id;
  j["kind"] = e.kind;
  j["timestamp"] = e.timestamp;
  j["direction"] = e.direction;
  j["status"] = e.status;

  std::visit([&j](auto&& arg) {
      using T = std::decay_t<decltype(arg)>;
      if constexpr (std::is_same_v<T, MessageEntry>) {
          j["content"] = arg.content;
      } else if constexpr (std::is_same_v<T, CallEntry>) {
          j["duration_seconds"] = arg.duration_seconds;
          j["call_direction"] = arg.call_direction;
          j["call_status"] = arg.call_status;
      }
  }, e.data);
}

inline void from_json(const nlohmann::json& j, HistoryEntryRecord& e) {
  j.at("id").get_to(e.id);
  j.at("kind").get_to(e.kind);
  j.at("timestamp").get_to(e.timestamp);
  e.direction = j.value("direction", MessageDirection::received);
  e.status = j.value("status", MessageStatus::None);
  switch (e.kind) {
    case HistoryEntryKind::TextMessage:
    case HistoryEntryKind::SystemMessage:
        e.data = MessageEntry{j.at("content").get<std::string>()};
        break;
    case HistoryEntryKind::VoiceCall:
    case HistoryEntryKind::VideoCall:
        e.data = CallEntry{j.value("duration_seconds", 0U), j.at("call_direction").get<CallDirection>(), j.at("call_status").get<CallStatus>()};
        break;
  }
}

struct ChatSummary {
    std::string peer_id;
    HistoryEntryRecord last_message;
    uint32_t unread_count = 0;
};

inline void to_json(nlohmann::json& j, const ChatSummary& s) {
    j["peer_id"] = s.peer_id;
    j["last_message"] = s.last_message;
    j["unread_count"] = s.unread_count;
}

struct HistoryPage {
    std::vector<HistoryEntryRecord> messages;
    size_t total_messages;
};

inline void to_json(nlohmann::json& j, const std::deque<HistoryEntryRecord>& deq) {
    j = nlohmann::json::array();
    for(const auto& msg : deq) {
        j.push_back(msg);
    }
}

inline void from_json(const nlohmann::json& j, std::deque<HistoryEntryRecord>& deq) {
    for (const auto& item : j) {
        deq.push_back(item.get<HistoryEntryRecord>());
    }
}

class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unordered_map<std::string, std::deque<HistoryEntryRecord>> history_cache_;
  mutable std::mutex mutex_;
  std::atomic<std::uint64_t> next_id_{1};
  std::function<void(const std::string& peer_id, std::uint64_t message_id, MessageStatus status)> on_status_changed_;

  static constexpr std::string GetStorageKey(std::string_view peer_id) {
    return std::string("history_") + std::string(peer_id);
  }

  void LoadHistory(const std::string& peer_id) {
    if (history_cache_.contains(peer_id)) {
      return;
    }

    auto key = GetStorageKey(peer_id);
    if (storage_->Exists(key)) {
      const auto data_opt = storage_->Load(key);
      if (data_opt) {
        try {
          const std::string json_str(reinterpret_cast<const char*>(data_opt->data()), data_opt->size());
          const nlohmann::json j = nlohmann::json::parse(json_str);
          history_cache_[peer_id] = j.get<std::deque<HistoryEntryRecord>>();
          if (!history_cache_.at(peer_id).empty()) { // NOLINT(*-pro-type-vararg)
            std::uint64_t max_id = 0;
            for (const auto& entry : history_cache_.at(peer_id)) {
                if (entry.id > max_id) {
                    max_id = entry.id;
                }
            }
            std::uint64_t current_next_id = next_id_.load();
            while (max_id >= current_next_id) {
                if (next_id_.compare_exchange_weak(current_next_id, max_id + 1)) {
                    break;
                }
            }
          }
        } catch (const nlohmann::json::parse_error& e) {
          LOGE << "Failed to parse message history for peer " << peer_id << ": " << e.what();
          storage_->Delete(key);
        }
      }
    }
  }

  void SaveHistory(const std::string& peer_id) {
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

  HistoryEntryRecord AddTextEntry(const std::string& peer_id, const std::string& content, MessageDirection direction) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory(peer_id);
    auto& history = history_cache_[peer_id];

    const HistoryEntryRecord entry{
        .id = next_id_.fetch_add(1),
        .kind = HistoryEntryKind::TextMessage,
        .timestamp = Time::Now(),
        .direction = direction,
        .status = (direction == MessageDirection::sent) ? MessageStatus::Sending : MessageStatus::None,
        .data = MessageEntry{content},
    };

    history.push_back(entry);
    SaveHistory(peer_id);
    return entry;
  }

  void AddCallEntry(const std::string& peer_id, HistoryEntryKind kind, CallDirection direction, CallStatus status, uint32_t duration = 0) {
      const std::lock_guard<std::mutex> lock(mutex_);
      LoadHistory(peer_id);
      auto& history = history_cache_[peer_id];

      history.push_back({
          .id = next_id_.fetch_add(1),
          .kind = kind,
          .timestamp = Time::Now(),
          .direction = (direction == CallDirection::Incoming) ? MessageDirection::received : MessageDirection::sent, // NOLINT(*-branch-clone)
          .data = CallEntry{duration, direction, status}
      });
      SaveHistory(peer_id);
  }

  void SetStatusChangedCallback(
    std::function<void(const std::string&, std::uint64_t, MessageStatus)> cb) {
      const std::lock_guard<std::mutex> lock(mutex_);
      on_status_changed_ = std::move(cb);
  }

  void UpdateMessageStatus(const std::string& peer_id, std::uint64_t message_id,
                           MessageStatus new_status) {
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      LoadHistory(peer_id);
      if (!history_cache_.contains(peer_id)) {
        return;
      }
      for (auto& history = history_cache_.at(peer_id);
           auto& entry : std::ranges::reverse_view(history)) {
        if (entry.id == message_id) {
          if (entry.status != new_status) {
            entry.status = new_status;
            SaveHistory(peer_id);
            if (on_status_changed_) {
              on_status_changed_(peer_id, message_id, new_status);
            }
          }
          return;
        }
      }
    }
  }

  HistoryPage GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) {
    const std::lock_guard<std::mutex> lock(mutex_);
    LoadHistory(peer_id);

    if (!history_cache_.contains(peer_id)) {
        return {{}, 0};
    }

    const auto& history = history_cache_.at(peer_id);
    const size_t total_size = history.size();

    const size_t end_offset = page_index * page_size;
    if (end_offset >= total_size) {
        return {{}, total_size};
    }

    const size_t start_offset = std::min(total_size, (page_index + 1) * page_size);

    const auto start_it = history.end() - start_offset;
    const auto end_it = history.end() - end_offset;

    std::vector<HistoryEntryRecord> messages_page(start_it, end_it);
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

    std::ranges::sort(summaries, [](const auto& a, const auto& b) {
        return a.last_message.timestamp > b.last_message.timestamp;
    });

    return summaries;
  }
};

}  // namespace eillance::messaging
