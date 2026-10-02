#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/channel_id.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/group_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

inline auto GenerateMessageId() -> std::uint64_t {
  std::uint64_t const now_ms = Time::Now() / 1000ULL;
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<uint64_t> dis(0, 524287);  // 19 bits
  return (now_ms << 19) | dis(gen);
}

class MessageHistory {
  std::shared_ptr<storage::PersistentStorage> storage_;

 public:
  explicit MessageHistory(std::shared_ptr<storage::PersistentStorage> storage)
      : storage_(std::move(storage)) {
  }

  auto AddTextEntry(TextEntryParams const& params) -> HistoryEntryRecord {
    // Received messages are by definition delivered. Sent messages start as sending.
    // We avoid MessageStatus::none for received messages to ensure they are visible in history queries immediately.
    MessageStatus initial_status =
        (params.direction == MessageDirection::sent) ? MessageStatus::sending : MessageStatus::delivered;

    if (params.kind == HistoryEntryKind::systemMessage) {
      initial_status = MessageStatus::none;
    }

    Time::Unit const ts = params.timestamp.value_or(Time::Now());

    std::uint64_t entry_id = params.id;
    if (entry_id == 0) {
      entry_id = GenerateMessageId();
    }

    HistoryEntryData data;
    if (storage_->GetGroup(GroupID(params.topic_id.Span()))) {
      data = GroupMessageEntry{MessageContent{params.content, params.attachments, params.reply_to_id}, {}};
    } else if (storage_->GetChannel(ChannelID(params.topic_id.Span()))) {
      data = ChannelMessageEntry{MessageContent{params.content, params.attachments, params.reply_to_id}};
    } else {
      data = ChatMessageEntry{MessageContent{params.content, params.attachments, params.reply_to_id}};
    }

    return storage_->AddHistoryEntry({
        .id = entry_id,
        .kind = params.kind,
        .timestamp = ts,
        .direction = params.direction,
        .status = initial_status,
        .is_truncated = false,
        .data = data,
        .topic_id = params.topic_id,
        .author_id = params.author_id.IsEmpty() ? ContactID(params.topic_id.Span()) : params.author_id,
    });
  }

  auto AddCallEntry(CallEntryParams const& params) -> HistoryEntryRecord {
    Time::Unit const ts = params.timestamp.value_or(Time::Now());

    std::uint64_t entry_id = params.id;
    if (entry_id == 0) {
      entry_id = GenerateMessageId();
    }

    return storage_->AddHistoryEntry({
        .id = entry_id,
        .kind = params.kind,
        .timestamp = ts,
        .direction =
            (params.direction == CallDirection::incoming) ? MessageDirection::received : MessageDirection::sent,
        .status = (params.direction == CallDirection::incoming && params.status == CallStatus::missed)
                      ? MessageStatus::delivered
                      : MessageStatus::read,
        .is_truncated = false,
        .data =
            CallEntry{
                .duration_seconds = params.duration_seconds,
                .call_direction = params.direction,
                .call_status = params.status
            },
        .topic_id = params.topic_id,
        .author_id = params.author_id.IsEmpty() ? ContactID(params.topic_id.Span()) : params.author_id,
    });
  }

  void UpdateHistoryEntry(HistoryEntryRecord const& entry) {
    storage_->UpdateHistoryEntry(entry);
  }

  void UpdateMessageStatus(TopicID const& topic_id, std::uint64_t const message_id, MessageStatus const new_status) {
    storage_->UpdateMessageStatus(topic_id, message_id, new_status);
    EventQueue::Instance().Push(EventMessageStatusChanged{topic_id, message_id, new_status});
  }

  void UpdateMessagePeerStatus(
      TopicID const& topic_id,
      std::uint64_t const message_id,
      ContactID const& peer_id,
      MessageStatus const new_status
  ) {
    storage_->UpdateMessagePeerStatus(topic_id, message_id, peer_id, new_status);
    EventQueue::Instance().Push(EventMessageUpdated{topic_id, message_id});
  }

  void UpdateMessageContent(TopicID const& topic_id, std::uint64_t message_id, std::string const& content) {
    storage_->UpdateMessageContent(topic_id, message_id, content);
  }

  void MarkMessageDeleted(TopicID const& topic_id, std::uint64_t message_id) {
    storage_->MarkMessageDeleted(topic_id, message_id);
  }

  void
  ToggleReaction(TopicID const& topic_id, std::uint64_t message_id, ContactID const& author_id, Reaction reaction) {
    storage_->ToggleReaction(topic_id, message_id, author_id, reaction);
  }

  [[nodiscard]] auto FindMessageIdByRemoteOrLocal(TopicID const& topic_id, std::uint64_t target_id) const
      -> std::optional<uint64_t> {
    return storage_->GetHistoryEntry(topic_id, target_id) ? std::make_optional(target_id) : std::nullopt;
  }

  void MarkMessagesAsRead(TopicID const& topic_id, std::vector<uint64_t> const& message_ids) {
    storage_->UpdateMessageStatuses(topic_id, message_ids, MessageStatus::read);
    for (auto const message_id : message_ids) {
      EventQueue::Instance().Push(EventMessageStatusChanged{topic_id, message_id, MessageStatus::read});
    }
  }

  [[nodiscard]] auto GetHistoryEntry(TopicID const& topic_id, std::uint64_t const message_id) const
      -> std::optional<messaging::HistoryEntryRecord> {
    return storage_->GetHistoryEntry(topic_id, message_id);
  }

  [[nodiscard]] auto GetHistoryPage(TopicID const& topic_id, size_t const page_index, size_t const page_size) const
      -> HistoryPage {
    return storage_->GetHistoryPage(topic_id, page_index, page_size);
  }

  [[nodiscard]] auto GetFullMessageContent(TopicID const& topic_id, std::uint64_t message_id) const
      -> std::optional<std::string> {
    return storage_->GetFullMessageContent(topic_id, message_id);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<std::string> const& topic_ids_str) const -> std::vector<ChatSummary> {
    std::vector<TopicID> topic_ids;
    topic_ids.reserve(topic_ids_str.size());
    for (auto const& topic_id : topic_ids_str) {
      topic_ids.emplace_back(topic_id);
    }
    return storage_->GetChatSummaries(topic_ids);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<TopicID> const& topic_ids) const -> std::vector<ChatSummary> {
    return storage_->GetChatSummaries(topic_ids);
  }

  [[nodiscard]] auto SearchMessages(TopicID const& topic_id, std::string const& query) const
      -> std::vector<HistoryEntryRecord> {
    return storage_->SearchMessages(topic_id, query);
  }

  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<HistoryEntryRecord> {
    return storage_->SearchAllMessages(query);
  }
};

}  // namespace eillance::messaging
