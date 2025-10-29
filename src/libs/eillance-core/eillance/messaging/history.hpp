#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::function<void(const std::string& peer_id, std::uint64_t message_id, MessageStatus status)> on_status_changed_;

public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) {}

  auto AddTextEntry(const std::string& peer_id, const std::string& content, MessageDirection direction) -> HistoryEntryRecord {
    const HistoryEntryRecord entry{
        .id = 0,  // DB will assign ID
        .kind = HistoryEntryKind::textMessage,
        .timestamp = Time::Now(),
        .direction = direction,
        .status = (direction == MessageDirection::sent) ? MessageStatus::sending : MessageStatus::none,
        .data = MessageEntry{content},
        .peer_id_for_db = peer_id,
    };
    return storage_->AddHistoryEntry(entry);
  }

  void AddCallEntry(const std::string& peer_id, const HistoryEntryKind kind, const CallDirection direction, const CallStatus status, uint32_t duration = 0) {
    storage_->AddHistoryEntry({.id = 0, .kind = kind, .timestamp = Time::Now(), .direction = (direction == CallDirection::incoming) ? MessageDirection::received : MessageDirection::sent, .data = CallEntry{.duration_seconds = duration, .call_direction = direction, .call_status = status}, .peer_id_for_db = peer_id});  // NOLINT(*-branch-clone)
  }

  void SetStatusChangedCallback(std::function<void(const std::string&, std::uint64_t, MessageStatus)> cb) {
    on_status_changed_ = std::move(cb);
  }

  void UpdateMessageStatus(const std::string& peer_id, const std::uint64_t message_id, const MessageStatus new_status) {
    storage_->UpdateMessageStatus(peer_id, message_id, new_status);
    if (on_status_changed_) { on_status_changed_(peer_id, message_id, new_status); }
  }

  void MarkMessagesAsRead(const std::string& peer_id, const std::vector<uint64_t>& message_ids) {
    storage_->UpdateMessageStatuses(peer_id, message_ids, MessageStatus::read);
    for (const auto message_id : message_ids) {
      if (on_status_changed_) { on_status_changed_(peer_id, message_id, MessageStatus::read); }
    }
  }

  auto GetHistoryPage(const std::string& peer_id, const size_t page_index, const size_t page_size) -> HistoryPage {
    return storage_->GetHistoryPage(peer_id, page_index, page_size);
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<ChatSummary> {
    return storage_->GetChatSummaries(peer_ids);
  }
};

}  // namespace eillance::messaging
