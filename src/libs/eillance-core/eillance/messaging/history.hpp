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
  std::function<void(ContactID const& peer_id, std::uint64_t message_id, MessageStatus status)> onStatusChanged_;

 public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {}

  auto AddTextEntry(
      ContactID const& peerId,
      std::string const& content,
      MessageDirection direction,
      HistoryEntryKind kind
  ) -> HistoryEntryRecord {
    HistoryEntryRecord const entry{
        .id = 0,  // DB will assign ID
        .kind = kind,
        .timestamp = static_cast<Time::Unit>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()
        ),
        .direction = direction,
        .status = (direction == MessageDirection::sent) ? MessageStatus::sending : MessageStatus::none,
        .is_truncated = false,
        .data = MessageEntry{content},
        .peer_id = peerId,
    };
    return storage_->AddHistoryEntry(entry);
  }

  auto AddCallEntry(
      ContactID const& peerId,
      HistoryEntryKind const kind,
      CallDirection const direction,
      CallStatus const status,
      uint32_t duration
  ) -> HistoryEntryRecord {
    return storage_->AddHistoryEntry(
        {.id = 0,
         .kind = kind,
         .timestamp = static_cast<Time::Unit>(
             std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                 .count()
         ),
         .direction = (direction == CallDirection::incoming) ? MessageDirection::received : MessageDirection::sent,
         .status = MessageStatus::none,
         .is_truncated = false,
         .data = CallEntry{.duration_seconds = duration, .call_direction = direction, .call_status = status},
         .peer_id = peerId}
    );
  }

  void SetStatusChangedCallback(std::function<void(ContactID const&, std::uint64_t, MessageStatus)> cb) {
    onStatusChanged_ = std::move(cb);
  }

  void UpdateHistoryEntry(HistoryEntryRecord const& entry) { storage_->UpdateHistoryEntry(entry); }

  void UpdateMessageStatus(ContactID const& peerId, std::uint64_t const message_id, MessageStatus const new_status) {
    storage_->UpdateMessageStatus(peerId, message_id, new_status);
    if (onStatusChanged_) {
      onStatusChanged_(peerId, message_id, new_status);
    }
  }

  void MarkMessagesAsRead(ContactID const& peerId, std::vector<uint64_t> const& messageIds) {
    storage_->UpdateMessageStatuses(peerId, messageIds, MessageStatus::read);
    for (auto const message_id : messageIds) {
      if (onStatusChanged_) {
        onStatusChanged_(peerId, message_id, MessageStatus::read);
      }
    }
  }

  auto GetHistoryPage(ContactID const& peerId, size_t const page_index, size_t const page_size) -> HistoryPage {
    return storage_->GetHistoryPage(peerId, page_index, page_size);
  }

  auto GetFullMessageContent(std::uint64_t messageId) -> std::optional<std::string> {
    return storage_->GetFullMessageContent(messageId);
  }

  auto GetChatSummaries(std::vector<std::string> const& peerIdsStr) -> std::vector<ChatSummary> {
    std::vector<ContactID> peer_ids;
    peer_ids.reserve(peerIdsStr.size());
    for (auto const& id : peerIdsStr) {
      peer_ids.emplace_back(id);
    }
    return storage_->GetChatSummaries(peer_ids);
  }

  auto GetChatSummaries(std::vector<ContactID> const& peerIds) -> std::vector<ChatSummary> {
    return storage_->GetChatSummaries(peerIds);
  }
};

}  // namespace eillance::messaging
