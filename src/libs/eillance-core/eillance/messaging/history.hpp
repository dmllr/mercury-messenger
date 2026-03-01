#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;

 public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
  }

  auto AddTextEntry(TextEntryParams const& params) -> HistoryEntryRecord {
    // Received messages are by definition delivered. Sent messages start as sending.
    // We avoid MessageStatus::none for received messages to ensure they are visible in history queries immediately.
    MessageStatus const initial_status =
        (params.direction == MessageDirection::sent) ? MessageStatus::sending : MessageStatus::delivered;

    return storage_->AddHistoryEntry({
        .id = 0,  // DB will assign ID
        .remote_id = params.remote_id,
        .kind = params.kind,
        .timestamp = static_cast<Time::Unit>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()
        ),
        .direction = params.direction,
        .status = initial_status,
        .is_truncated = false,
        .data = MessageEntry{params.content, params.attachments},
        .peer_id = params.peer_id,
    });
  }

  auto AddCallEntry(CallEntryParams const& params) -> HistoryEntryRecord {
    return storage_->AddHistoryEntry({
        .id = 0,
        .kind = params.kind,
        .timestamp = static_cast<Time::Unit>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count()
        ),
        .direction =
            (params.direction == CallDirection::incoming) ? MessageDirection::received : MessageDirection::sent,
        .status = MessageStatus::none,
        .is_truncated = false,
        .data =
            CallEntry{
                .duration_seconds = params.duration_seconds,
                .call_direction = params.direction,
                .call_status = params.status
            },
        .peer_id = params.peer_id,
    });
  }

  void UpdateHistoryEntry(HistoryEntryRecord const& entry) {
    storage_->UpdateHistoryEntry(entry);
  }

  void UpdateMessageStatus(ContactID const& peer_id, std::uint64_t const message_id, MessageStatus const new_status) {
    storage_->UpdateMessageStatus(peer_id, message_id, new_status);
    EventQueue::Instance().Push(EventMessageStatusChanged{peer_id, message_id, new_status});
  }

  void UpdateMessageContent(ContactID const& peer_id, std::uint64_t message_id, std::string const& content) {
    storage_->UpdateMessageContent(peer_id, message_id, content);
  }

  void MarkMessageDeleted(ContactID const& peer_id, std::uint64_t message_id) {
    storage_->MarkMessageDeleted(peer_id, message_id);
  }

  void
  ToggleReaction(ContactID const& peer_id, std::uint64_t message_id, ContactID const& author_id, Reaction reaction) {
    storage_->ToggleReaction(peer_id, message_id, author_id, reaction);
  }

  [[nodiscard]] auto FindMessageIdByRemoteOrLocal(ContactID const& peer_id, uint64_t target_id) const
      -> std::optional<uint64_t> {
    return storage_->GetMessageIdByRemoteId(peer_id, target_id);
  }

  [[nodiscard]] auto GetRemoteId(ContactID const& peer_id, uint64_t local_id) const -> std::optional<uint64_t> {
    auto const entry = storage_->GetHistoryEntry(peer_id, local_id);
    return entry ? entry->remote_id : std::nullopt;
  }

  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) {
    storage_->UpdateMessageStatuses(peer_id, message_ids, MessageStatus::read);
    for (auto const message_id : message_ids) {
      EventQueue::Instance().Push(EventMessageStatusChanged{peer_id, message_id, MessageStatus::read});
    }
  }

  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t const page_index, size_t const page_size) const
      -> HistoryPage {
    return storage_->GetHistoryPage(peer_id, page_index, page_size);
  }

  [[nodiscard]] auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id) const
      -> std::optional<std::string> {
    return storage_->GetFullMessageContent(peer_id, message_id);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<std::string> const& peer_ids_str) const -> std::vector<ChatSummary> {
    std::vector<ContactID> peer_ids;
    peer_ids.reserve(peer_ids_str.size());
    for (auto const& peer_id : peer_ids_str) {
      peer_ids.emplace_back(peer_id);
    }
    return storage_->GetChatSummaries(peer_ids);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids) const -> std::vector<ChatSummary> {
    return storage_->GetChatSummaries(peer_ids);
  }

  [[nodiscard]] auto SearchMessages(ContactID const& peer_id, std::string const& query) const
      -> std::vector<HistoryEntryRecord> {
    return storage_->SearchMessages(peer_id, query);
  }

  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<HistoryEntryRecord> {
    return storage_->SearchAllMessages(query);
  }
};

}  // namespace eillance::messaging
