#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::function<void(const ContactID& peer_id, std::uint64_t message_id, MessageStatus status)> on_status_changed_;

public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage) : storage_(std::move(storage)) {}

  auto AddTextEntry(const ContactID& peer_id, const std::string& content, MessageDirection direction, HistoryEntryKind kind = HistoryEntryKind::textMessage) -> HistoryEntryRecord {
    const HistoryEntryRecord entry{
        .id = 0,  // DB will assign ID
        .kind = kind,
        .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),
        .direction = direction,
        .status = (direction == MessageDirection::sent) ? MessageStatus::sending : MessageStatus::none,
        .is_truncated = false,
        .data = MessageEntry{content},
        .peer_id_for_db = peer_id.ToString(),
    };
    return storage_->AddHistoryEntry(entry);
  }

  void AddCallEntry(const ContactID& peer_id, const HistoryEntryKind kind, const CallDirection direction, const CallStatus status, uint32_t duration = 0) {
    storage_->AddHistoryEntry({.id = 0, .kind = kind, .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()), .direction = (direction == CallDirection::incoming) ? MessageDirection::received : MessageDirection::sent, .data = CallEntry{.duration_seconds = duration, .call_direction = direction, .call_status = status}, .peer_id_for_db = peer_id.ToString()});  // NOLINT(*-branch-clone)
  }

  void SetStatusChangedCallback(std::function<void(const ContactID&, std::uint64_t, MessageStatus)> cb) {
    on_status_changed_ = std::move(cb);
  }

  void UpdateHistoryEntry(const HistoryEntryRecord& entry) {
    storage_->UpdateHistoryEntry(entry);
  }

  void UpdateMessageStatus(const ContactID& peer_id, const std::uint64_t message_id, const MessageStatus new_status) {
    storage_->UpdateMessageStatus(peer_id, message_id, new_status);
    if (on_status_changed_) { on_status_changed_(peer_id, message_id, new_status); }
  }

  void MarkMessagesAsRead(const ContactID& peer_id, const std::vector<uint64_t>& message_ids) {
    storage_->UpdateMessageStatuses(peer_id, message_ids, MessageStatus::read);
    for (const auto message_id: message_ids) {
      if (on_status_changed_) { on_status_changed_(peer_id, message_id, MessageStatus::read); }
    }
  }

  auto GetHistoryPage(const ContactID& peer_id, const size_t page_index, const size_t page_size) -> HistoryPage {
    return storage_->GetHistoryPage(peer_id, page_index, page_size);
  }

  auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> {
    return storage_->GetFullMessageContent(message_id);
  }

  auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<ChatSummary> {
    return storage_->GetChatSummaries(peer_ids);
  }
};

}  // namespace eillance::messaging
