#pragma once

#ifdef _WIN32
#define NOMINMAX
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/identity/channel_manager.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/identity/group_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

namespace {

constexpr size_t maxAttachmentNoteLength = 120;

auto GetExtensionForMimeType(std::string_view const mime_type) -> std::string {
  // NOLINTBEGIN(*-braces-around-statements)
  if (mime_type == "image/jpeg" || mime_type == "image/jpg") return ".jpg";
  if (mime_type == "image/png") return ".png";
  if (mime_type == "image/gif") return ".gif";
  if (mime_type == "image/webp") return ".webp";
  if (mime_type == "video/mp4") return ".mp4";
  if (mime_type == "video/webm") return ".webm";
  if (mime_type == "video/quicktime") return ".mov";
  if (mime_type == "video/x-matroska") return ".mkv";
  if (mime_type == "audio/mpeg") return ".mp3";
  if (mime_type == "audio/ogg") return ".ogg";
  if (mime_type == "audio/wav") return ".wav";
  if (mime_type == "application/pdf") return ".pdf";
  if (mime_type == "text/plain") return ".txt";
  // NOLINTEND(*-braces-around-statements)
  return ".dat";
}

auto ResolvePathConflict(std::filesystem::path const& dir, std::string const& filename) -> std::filesystem::path {
  std::filesystem::path candidate = dir / filename;
  if (!std::filesystem::exists(candidate)) {
    return candidate;
  }

  std::string const stem = std::filesystem::path(filename).stem().string();
  std::string const ext = std::filesystem::path(filename).extension().string();

  int counter = 1;
  while (true) {
    std::string const new_name = stem + "(" + std::to_string(counter) + ")" + ext;
    candidate = dir / new_name;
    if (!std::filesystem::exists(candidate)) {
      return candidate;
    }
    counter++;
  }
}

}  // namespace

class MessagingService {
  using Json = nlohmann::json;

 public:
  static constexpr std::uint16_t protocolVersion = 2;
  static constexpr std::uint16_t minSupportedVersion = 1;
  static constexpr auto keyVersion = "v";
  static constexpr auto keyContent = "c";

  struct Serializer {
    static auto BuildE2EEPayload(std::string const& content) -> std::string {
      Json payload_json;
      payload_json[MessagingService::keyVersion] = MessagingService::protocolVersion;
      payload_json[MessagingService::keyContent] = content;
      return payload_json.dump();
    }
  };

  explicit MessagingService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport),
        contact_manager_(sm->contact_manager) {
    history_ = std::make_unique<messaging::MessageHistory>(storage_);

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSyncStateChanged>([this](auto const& e) -> void {
      bool evaluate_unread = false;
      bool evaluate_missed_calls = false;

      {
        std::lock_guard<std::mutex> const lock(sync_state_mutex_);
        is_syncing_[e.peer_id] = e.is_syncing;
        if (!e.is_syncing) {
          TopicID const topic_id(e.peer_id);
          if (pending_delivery_receipts_.contains(topic_id) && !pending_delivery_receipts_[topic_id].empty()) {
            if (!delivery_receipts_debounce_active_[e.peer_id]) {
              delivery_receipts_debounce_active_[e.peer_id] = true;
              bg_queue_.DispatchAfter(std::chrono::milliseconds(250), [this, topic_id]() {
                FlushDeliveryReceipts(topic_id);
              });
            }
          }
          if (unread_evaluation_needed_[e.peer_id]) {
            evaluate_unread = true;
            unread_evaluation_needed_[e.peer_id] = false;
          }
          if (missed_call_evaluation_needed_[e.peer_id]) {
            evaluate_missed_calls = true;
            missed_call_evaluation_needed_[e.peer_id] = false;
          }
        }
      }

      if ((evaluate_unread || evaluate_missed_calls) && !sm_->is_foreground) {
        bg_queue_.Dispatch([this, peer_id = e.peer_id, evaluate_unread, evaluate_missed_calls]() {
          EvaluateNotifications(TopicID(peer_id), evaluate_unread, evaluate_missed_calls);
        });
      }
    }));
  }

  ~MessagingService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  auto SendChatMessage(
      TopicID const& topic_id,
      std::string const& message,
      std::vector<messaging::AttachmentRequest> const& attachments = {},
      std::uint64_t reply_to_id = 0
  ) -> messaging::HistoryEntryRecord {
    auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
    auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()));
    if (!group && !channel && !contact_manager_->GetContact(ContactID(topic_id.Span()))) {
      LGE << "Cannot send message. Topic not found.";
      return {};
    }

    auto domain_attachments = ProcessOutgoingAttachments(attachments);

    auto entry = history_->AddTextEntry(
        {.topic_id = topic_id,
         .content = message,
         .attachments = domain_attachments,
         .direction = messaging::MessageDirection::sent,
         .kind = messaging::HistoryEntryKind::textMessage,
         .reply_to_id = reply_to_id,
         .author_id = sm_->account->GetId()}
    );

    Json payload_json;
    payload_json[keyVersion] = protocolVersion;
    payload_json[keyContent] = message;
    if (reply_to_id > 0) {
      payload_json["reply_to_id"] = reply_to_id;
    }

    Json json_msg = {
        {"type", "chat"},
        {"message_id", entry.id},
        {"timestamp", entry.timestamp},
        {"is_system", false},
        {"payload", payload_json}
    };

    if (!domain_attachments.empty()) {
      std::vector<Json> att_list;
      att_list.reserve(domain_attachments.size());
      for (auto const& att : domain_attachments) {
        att_list.push_back(
            {{"id", att.id},
             {"type", att.type},
             {"mime", att.mime_type},
             {"size", att.size_bytes},
             {"thumb", att.thumbnail_base64},
             {"hash", att.hash},
             {"note", att.note},
             {"name", att.file_name}}
        );
      }
      json_msg["attachments"] = att_list;
    }

    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Message, json_msg);

    for (auto const& att : domain_attachments) {
      auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
      if (group) {
        for (auto const& m : group->members) {
          if (m.contact_id != sm_->account->GetId()) sm_->file_transfer->QueueUpload(m.contact_id, att);
        }
      } else if (auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()))) {
        for (auto const& m : channel->members) {
          if (m.contact_id != sm_->account->GetId()) sm_->file_transfer->QueueUpload(m.contact_id, att);
        }
      } else {
        sm_->file_transfer->QueueUpload(ContactID(topic_id.Span()), att);
      }
    }

    EventQueue::Instance().Push(EventMessageUpdated{topic_id, entry.id});

    return entry;
  }

  void SendDeleteMessage(TopicID const& topic_id, std::uint64_t message_id) {
    history_->MarkMessageDeleted(topic_id, message_id);

    Json const action = {{"type", "message_delete"}, {"target_id", message_id}};

    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::MessageDelete, action);
    EventQueue::Instance().Push(EventMessageUpdated{topic_id, message_id});
  }

  void SendEditMessage(TopicID const& topic_id, std::uint64_t message_id, std::string const& new_content) {
    history_->UpdateMessageContent(topic_id, message_id, new_content);

    Json const action = {{"type", "message_edit"}, {"target_id", message_id}, {"content", new_content}};

    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::MessageEdit, action);
    EventQueue::Instance().Push(EventMessageUpdated{topic_id, message_id});
  }

  void SendReaction(TopicID const& topic_id, std::uint64_t message_id, messaging::Reaction reaction) {
    history_->ToggleReaction(topic_id, message_id, sm_->account->GetId(), reaction);

    Json const action = {
        {"type", "message_reaction"},
        {"target_id", message_id},
        {"reaction", static_cast<int>(reaction)}
    };

    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Reaction, action);
    EventQueue::Instance().Push(EventMessageUpdated{topic_id, message_id});
  }

  void SendSystemMessage(TopicID const& topic_id, std::string const& content, bool persist) {
    auto const entry = history_->AddTextEntry(
        {.topic_id = topic_id,
         .content = content,
         .direction = messaging::MessageDirection::sent,
         .kind = messaging::HistoryEntryKind::systemMessage}
    );

    EventQueue::Instance().Push(EventChatMessageReceived{topic_id, entry});

    if (persist) {
      Json const action_payload = {
          {"type", "chat"},
          {"message_id", entry.id},
          {"timestamp", entry.timestamp},
          {"is_system", true},
          {"payload", {{"c", content}, {"v", protocolVersion}}}
      };
      sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Message, action_payload);
    }
  }

  void CancelTransfer(
      TopicID const& topic_id,
      std::string const& attachment_id,
      bool const is_upload,
      bool const notify_peer = true
  ) const {
    sm_->file_transfer->CancelTransfer(attachment_id, is_upload, notify_peer);

    auto const new_status = (!is_upload) ? messaging::AttachmentStatus::paused : messaging::AttachmentStatus::canceled;
    EventQueue::Instance().Push(EventAttachmentStatusChanged{topic_id, attachment_id, new_status});

    if (is_upload) {
      auto const sending_msgs = storage_->GetMessagesWithStatus(topic_id, messaging::MessageStatus::sending);
      for (auto const& entry : sending_msgs) {
        if (auto const* content = messaging::GetMessageContent(entry.data)) {
          bool contains_attachment = false;
          bool has_other_content = !content->content.empty();
          for (auto const& att : content->attachments) {
            if (att.id == attachment_id) {
              contains_attachment = true;
            } else {
              if (auto const stored = storage_->GetAttachment(att.id);
                  stored && stored->status != messaging::AttachmentStatus::canceled &&
                  stored->status != messaging::AttachmentStatus::discarded) {
                has_other_content = true;
              }
            }
          }
          if (contains_attachment && !has_other_content) {
            history_->UpdateMessageStatus(topic_id, entry.id, messaging::MessageStatus::none);
            break;
          }
        }
      }
    }
  }

  void ResumeTransfer(TopicID const& topic_id, std::string const& attachment_id, bool const is_upload) {
    sm_->file_transfer->ResumeTransfer(attachment_id, is_upload);
    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{topic_id, attachment_id, messaging::AttachmentStatus::pending}
    );

    auto att = storage_->GetAttachment(attachment_id);
    if (!att) return;

    auto enqueue = [&](ContactID const& target) {
      if (is_upload)
        sm_->file_transfer->QueueUpload(target, *att);
      else
        sm_->file_transfer->QueueDownload(target, *att, 0);

      for (auto const& [net_id, peer] : transport_->GetPeers()) {
        if (transport_->GetMasterId(net_id) == target) {
          if (peer->data_channel_is_open && peer->crcp_verified) {
            sm_->sync_engine->ProposeSync(topic_id, net_id);
          }
        }
      }
    };

    auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
    if (group) {
      for (auto const& m : group->members) {
        if (m.contact_id != sm_->account->GetId()) enqueue(m.contact_id);
      }
    } else if (auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()))) {
      for (auto const& m : channel->members) {
        if (m.contact_id != sm_->account->GetId()) enqueue(m.contact_id);
      }
    } else {
      enqueue(ContactID(topic_id.Span()));
    }
  }

  void DiscardAttachment(TopicID const& topic_id, std::string const& attachment_id) {
    sm_->file_transfer->DiscardAttachment(attachment_id);
    auto const attachment = storage_->GetAttachment(attachment_id);
    bool should_send_cancel = true;
    if (attachment) {
      if (attachment->status == messaging::AttachmentStatus::canceled ||
          attachment->status == messaging::AttachmentStatus::peerCanceled ||
          attachment->status == messaging::AttachmentStatus::failed) {
        should_send_cancel = false;
      }
    }

    if (should_send_cancel) {
      Json const cancel_msg = {{"type", "file_cancel"}, {"file_id", attachment_id}};
      sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Message, cancel_msg);
    }

    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{
            topic_id,
            attachment_id,
            should_send_cancel ? messaging::AttachmentStatus::canceled : messaging::AttachmentStatus::discarded
        }
    );
  }

  [[nodiscard]] auto GetDownloadedAttachments(TopicID const& topic_id) const
      -> std::vector<messaging::AttachmentRecord> {
    return storage_->GetDownloadedAttachments(topic_id);
  }

  void DeleteAttachmentLocal(TopicID const& topic_id, std::string const& attachment_id) const {
    auto att = storage_->GetAttachment(attachment_id);
    if (att && att->is_downloaded) {
      std::error_code ec;
      std::filesystem::remove(att->file_path, ec);
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::discarded, false);
      EventQueue::Instance().Push(
          EventAttachmentStatusChanged{topic_id, attachment_id, messaging::AttachmentStatus::discarded}
      );
    }
  }

  [[nodiscard]] auto GetHistoryPage(TopicID const& topic_id, size_t page_index, size_t const page_size) const
      -> messaging::HistoryPage {
    auto page = history_->GetHistoryPage(topic_id, page_index, page_size);
    std::erase_if(page.messages, [](auto const& entry) {
      if (entry.status == messaging::MessageStatus::none && entry.kind != messaging::HistoryEntryKind::systemMessage &&
          entry.kind != messaging::HistoryEntryKind::voiceCall &&
          entry.kind != messaging::HistoryEntryKind::videoCall) {
        return true;
      }
      if (entry.kind == messaging::HistoryEntryKind::systemMessage &&
          messaging::GetMessageContent(entry.data) != nullptr) {
        auto const& content = messaging::GetMessageContent(entry.data)->content;
        if (content.find("\"action\":\"call_request\"") != std::string::npos ||
            content.find("\"action\":\"call_accept\"") != std::string::npos) {
          return true;
        }
      }
      return false;
    });

    for (auto& entry : page.messages) {
      if (auto* content = messaging::GetMessageContent(entry.data)) {
        bool entry_modified = false;
        for (auto& att : content->attachments) {
          if (att.status == messaging::AttachmentStatus::canceled ||
              att.status == messaging::AttachmentStatus::peerCanceled ||
              att.status == messaging::AttachmentStatus::paused ||
              att.status == messaging::AttachmentStatus::discarded) {
            continue;
          }

          if (!att.is_downloaded && !att.file_path.empty()) {
            if (std::error_code err; std::filesystem::exists(att.file_path, err) &&
                                     std::filesystem::is_regular_file(att.file_path, err) &&
                                     std::filesystem::file_size(att.file_path, err) == att.size_bytes) {
              att.is_downloaded = true;
              att.status = messaging::AttachmentStatus::done;
              entry_modified = true;
            }
          }
        }
        if (entry_modified) {
          history_->UpdateHistoryEntry(entry);
        }
      }
    }
    return page;
  }

  [[nodiscard]] auto GetFullMessageContent(TopicID const& topic_id, std::uint64_t message_id) const
      -> std::optional<std::string> {
    return history_->GetFullMessageContent(topic_id, message_id);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<TopicID> const& topic_ids) const
      -> std::vector<messaging::ChatSummary> {
    return history_->GetChatSummaries(topic_ids);
  }

  [[nodiscard]] auto GetUnreadMessages(TopicID const& topic_id) const -> std::vector<messaging::HistoryEntryRecord> {
    auto messages = storage_->GetMessagesWithStatus(topic_id, messaging::MessageStatus::delivered);
    std::erase_if(messages, [](auto const& msg) -> bool {
      return msg.direction != messaging::MessageDirection::received;
    });
    std::ranges::sort(messages, {}, &messaging::HistoryEntryRecord::timestamp);
    return messages;
  }

  [[nodiscard]] auto SearchMessages(TopicID const& topic_id, std::string const& query) const
      -> std::vector<messaging::HistoryEntryRecord> {
    return history_->SearchMessages(topic_id, query);
  }

  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<messaging::HistoryEntryRecord> {
    return history_->SearchAllMessages(query);
  }

  void MarkMessagesAsRead(TopicID const& topic_id, std::vector<std::uint64_t> const& message_ids) {
    history_->MarkMessagesAsRead(topic_id, message_ids);
    if (message_ids.empty()) {
      return;
    }
    Json const msg = {{"type", "message_read"}, {"message_ids", message_ids}};
    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Message, msg);
  }

  // --- Read Model Projections ---
  void ProjectMessage(messaging::LedgerEntry const& entry, Json const& json, bool is_mine, bool is_synced = false) {
    TopicID topic_id = entry.topic_id;
    if (topic_id == TopicID(sm_->account->GetId())) {
      topic_id = TopicID(entry.author_id);
    }

    std::string const type = json.value("type", "");

    bool is_contact_request = false;
    if (type == "chat") {
      auto payload = json.value("payload", Json::object());
      std::string const content = payload.value(keyContent, "");
      if (content.find("\"action\":\"contact_request\"") != std::string::npos) {
        is_contact_request = true;
      }
    }

    auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
    auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()));
    if (!group && !channel && !contact_manager_->GetContact(ContactID(topic_id.Span()))) {
      contact_manager_->AddContact(ContactID(topic_id.Span()));
      if (!is_contact_request) {
        contact_manager_->MarkAsConfirmed(ContactID(topic_id.Span()));
      }
      EventQueue::Instance().Push(EventContactRestored{ContactID(topic_id.Span())});
    }

    if (channel) {
      bool is_admin = (channel->owner_id == entry.author_id);
      for (auto const& m : channel->members) {
        if (m.contact_id == entry.author_id && m.role == 1) {
          is_admin = true;
          break;
        }
      }
      if (!is_admin) {
        LGE << "CRCP Enforcement: Dropped message from non-admin in channel " << topic_id.ToString();
        return;
      }
    }

    if (type == "message_read" || type == "message_receipt") {
      HandleReceiptProjection(topic_id, entry, json, is_mine, is_synced, type);
      return;
    }

    if (type == "file_cancel") {
      sm_->file_transfer->HandleCancel(ContactID(topic_id.Span()), json.value("file_id", ""));
      return;
    }

    if (type == "chat") {
      HandleChatProjection(topic_id, entry, json, is_mine);
    }
  }

  void ProjectEdit(messaging::LedgerEntry const& entry, Json const& json, bool /*is_mine*/) {
    TopicID topic_id = entry.topic_id;
    if (topic_id == TopicID(sm_->account->GetId())) {
      topic_id = TopicID(entry.author_id);
    }
    uint64_t const target_id = json.value("target_id", 0ULL);
    std::string const content = json.value("content", "");
    if (history_->GetHistoryEntry(topic_id, target_id)) {
      history_->UpdateMessageContent(topic_id, target_id, content);
      EventQueue::Instance().Push(EventMessageUpdated{topic_id, target_id});
    }
  }

  void ProjectDelete(messaging::LedgerEntry const& entry, Json const& json, bool /*is_mine*/) {
    TopicID topic_id = entry.topic_id;
    if (topic_id == TopicID(sm_->account->GetId())) {
      topic_id = TopicID(entry.author_id);
    }
    uint64_t const target_id = json.value("target_id", 0ULL);
    if (history_->GetHistoryEntry(topic_id, target_id)) {
      history_->MarkMessageDeleted(topic_id, target_id);
      EventQueue::Instance().Push(EventMessageUpdated{topic_id, target_id});
    }
  }

  void ProjectReaction(messaging::LedgerEntry const& entry, Json const& json, bool /*is_mine*/) {
    TopicID topic_id = entry.topic_id;
    if (topic_id == TopicID(sm_->account->GetId())) {
      topic_id = TopicID(entry.author_id);
    }
    uint64_t const target_id = json.value("target_id", 0ULL);
    auto const reaction = static_cast<messaging::Reaction>(json.value("reaction", 0));
    if (history_->GetHistoryEntry(topic_id, target_id)) {
      history_->ToggleReaction(topic_id, target_id, entry.author_id, reaction);
      EventQueue::Instance().Push(EventMessageUpdated{topic_id, target_id});
    }
  }

  void ProjectCallLog(messaging::LedgerEntry const& entry, Json const& json, bool is_mine, bool is_synced = false) {
    TopicID topic_id = entry.topic_id;
    if (topic_id == TopicID(sm_->account->GetId())) {
      topic_id = TopicID(entry.author_id);
    }

    auto ledger_ts = static_cast<uint64_t>(entry.created_at.value);
    if (ledger_ts < 10000000000ULL) {
      ledger_ts *= 1000000ULL;
    } else if (ledger_ts < 10000000000000ULL) {
      ledger_ts *= 1000ULL;
    }

    uint64_t msg_timestamp = json.value("timestamp", ledger_ts);

    // Simple deduplication for recent call logs to prevent double-entry for answered calls
    auto [messages, total_messages] = history_->GetHistoryPage(topic_id, 0, 10);
    for (auto const& msg : messages) {
      if (msg.kind == messaging::HistoryEntryKind::voiceCall || msg.kind == messaging::HistoryEntryKind::videoCall) {
        if (abs(static_cast<std::int64_t>(msg.timestamp - msg_timestamp)) < 30000000ULL) {
          return;  // Duplicate
        }
      }
    }

    messaging::CallEntryParams const params{
        .topic_id = topic_id,
        .kind = json.value("is_video", false) ? messaging::HistoryEntryKind::videoCall
                                              : messaging::HistoryEntryKind::voiceCall,
        .direction = is_mine ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
        .status = static_cast<messaging::CallStatus>(json.value("call_status", 0)),
        .duration_seconds = json.value("duration_seconds", 0U),
        .id = json.value("message_id", 0ULL),
        .timestamp = msg_timestamp,
        .author_id = entry.author_id
    };
    auto const db_entry = history_->AddCallEntry(params);
    EventQueue::Instance().Push(EventChatMessageReceived{topic_id, db_entry});

    if (!is_mine && params.status == messaging::CallStatus::missed) {
      bool should_evaluate_now = false;
      {
        std::lock_guard<std::mutex> const lock(sync_state_mutex_);
        if (!is_syncing_[ContactID(topic_id.Span())]) {
          should_evaluate_now = true;
        } else {
          missed_call_evaluation_needed_[ContactID(topic_id.Span())] = true;
        }
      }
      if (should_evaluate_now && !sm_->is_foreground) {
        bg_queue_.Dispatch([this, topic_id]() { EvaluateNotifications(topic_id, false, true); });
      }
    }
  }

  void AddCallEntry(messaging::CallEntryParams const& params, bool sync_to_ledger) {
    auto const entry = history_->AddCallEntry(params);
    if (sync_to_ledger) {
      Json const payload = {
          {"is_video", params.kind == messaging::HistoryEntryKind::videoCall},
          {"duration_seconds", params.duration_seconds},
          {"call_status", static_cast<int>(params.status)},
          {"timestamp", entry.timestamp}
      };
      sm_->sync_engine->AppendAction(params.topic_id, messaging::LedgerActionType::CallLog, payload);
    }
    EventQueue::Instance().Push(EventChatMessageReceived{params.topic_id, entry});
  }

  void
  AddSystemEntry(TopicID const& topic_id, std::string const& content, messaging::MessageDirection const direction) {
    auto const entry = history_->AddTextEntry(
        {.topic_id = topic_id,
         .content = content,
         .direction = direction,
         .kind = messaging::HistoryEntryKind::systemMessage,
         .author_id = sm_->account->GetId()}
    );
    EventQueue::Instance().Push(EventChatMessageReceived{topic_id, entry});
  }

  void AddCallEntry(messaging::CallEntryParams const& params) {
    auto const entry = history_->AddCallEntry(params);
    EventQueue::Instance().Push(EventChatMessageReceived{params.topic_id, entry});
  }

  void OnChatMessageReceived(
      TopicID const& topic_id,
      Json const& payload,
      uint64_t message_id,
      bool is_system,
      std::vector<messaging::Attachment> const& attachments = {}
  ) {
    ContactID const peer_id(topic_id.Span());
    auto peer = transport_->GetPeer(peer_id);
    if (!peer || !peer->session_cipher->IsReady()) {
      return;
    }

    if (!contact_manager_->GetContact(peer_id)) {
      contact_manager_->MarkAsConfirmed(peer_id);
      EventQueue::Instance().Push(EventContactRestored{peer_id});
    }

    if (auto const p = transport_->GetPeer(peer_id)) {
      p->last_activity_time = Time::SteadyNow();
    }

    if (history_->GetHistoryEntry(topic_id, message_id)) {
      QueueDeliveryReceipt(topic_id, message_id);
      return;
    }

    std::string content;
    try {
      if (auto const received_version = payload.value(keyVersion, 0); received_version < minSupportedVersion) {
        throw std::runtime_error("Message version too old: " + std::to_string(received_version));
      }
      content = payload.at(keyContent).get<std::string>();
    } catch (std::exception const&) {
      Json const error_json = {{"action", "unsupported_format"}};
      auto entry = history_->AddTextEntry(
          {.topic_id = topic_id,
           .content = error_json.dump(),
           .direction = messaging::MessageDirection::received,
           .kind = messaging::HistoryEntryKind::systemMessage}
      );
      QueueDeliveryReceipt(topic_id, message_id);
      EventQueue::Instance().Push(EventChatMessageReceived{topic_id, entry});
      return;
    }

    auto const local_attachments = ProcessIncomingAttachments(attachments);

    uint64_t local_reply_to_id = 0;
    try {
      if (payload.contains("reply_to_id")) {
        local_reply_to_id = payload["reply_to_id"].get<uint64_t>();
      }
    } catch (...) {
    }

    auto entry = history_->AddTextEntry(
        {.topic_id = topic_id,
         .content = content,
         .attachments = local_attachments,
         .direction = messaging::MessageDirection::received,
         .kind = is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage,
         .id = message_id,
         .reply_to_id = local_reply_to_id}
    );

    if (!local_attachments.empty()) {
      for (auto const& att : local_attachments) {
        sm_->file_transfer->QueueDownload(ContactID(topic_id.Span()), att, entry.id);
      }
    }

    QueueDeliveryReceipt(topic_id, message_id);

    EventQueue::Instance().Push(EventChatMessageReceived{topic_id, entry});

    TriggerNewMessageNotificationIfNeeded(topic_id, is_system, entry.id);
  }

 private:
  auto ProcessOutgoingAttachments(std::vector<messaging::AttachmentRequest> const& attachments)
      -> std::vector<messaging::Attachment> {
    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    std::error_code dir_ec;
    std::filesystem::create_directories(attachments_dir, dir_ec);

    std::vector<messaging::Attachment> domain_attachments;
    for (auto const& req : attachments) {
      std::uint64_t file_size = 0;
      std::string mime = req.mime_type;
      std::string hash;

      std::error_code ec;
      if (!std::filesystem::exists(req.file_path, ec) || ec) {
        LGW << "Attachment file does not exist: " << req.file_path;
        continue;
      }
      try {
        file_size = std::filesystem::file_size(req.file_path, ec);
        if (ec) {
          LGW << "Failed to get file size for: " << req.file_path;
          continue;
        }
        hash = crypto::CalculateFileHash(req.file_path);
      } catch (...) {
        LGW << "Failed to process attachment file: " << req.file_path;
        continue;
      }

      if (hash.empty()) {
        LGW << "Failed to calculate hash for attachment (empty result): " << req.file_path;
        continue;
      }

      if (mime.empty()) {
        if (req.type == messaging::AttachmentType::image) {
          mime = "image/jpeg";
        } else if (req.type == messaging::AttachmentType::video) {
          mime = "video/mp4";
        } else {
          mime = "application/octet-stream";
        }
      }

      std::string filename = req.file_name;
      if (filename.empty()) {
        filename = GenerateAttachmentId() + GetExtensionForMimeType(mime);
      }

      std::filesystem::path final_path = req.file_path;
      std::filesystem::path const req_path(req.file_path);

      if (req_path.parent_path() != attachments_dir) {
        final_path = ResolvePathConflict(attachments_dir, filename);
        std::filesystem::rename(req_path, final_path, ec);
        if (ec) {
          ec.clear();
          std::filesystem::copy_file(req_path, final_path, std::filesystem::copy_options::overwrite_existing, ec);
          if (!ec) {
            std::filesystem::remove(req_path, ec);
          }
        }
        if (ec) {
          LGW << "Failed to move attachment to persistent dir: " << ec.message();
          continue;
        }
      }

      std::string note = req.note;
      if (note.length() > maxAttachmentNoteLength) {
        note = note.substr(0, maxAttachmentNoteLength);
      }

      domain_attachments.push_back(
          {.id = GenerateAttachmentId(),
           .type = req.type,
           .file_path = final_path.string(),
           .mime_type = mime,
           .size_bytes = file_size,
           .thumbnail_base64 = req.thumbnail_base64,
           .hash = hash,
           .is_downloaded = true,
           .status = messaging::AttachmentStatus::pending,
           .note = note,
           .file_name = req.file_name}
      );
    }
    return domain_attachments;
  }

  void HandleReceiptProjection(
      TopicID const& topic_id,
      messaging::LedgerEntry const& entry,
      Json const& json,
      bool is_mine,
      bool is_synced,
      std::string const& type
  ) {
    if (is_mine && !is_synced) {
      return;
    }
    auto msg_status = messaging::MessageStatus::sending;
    if (type == "message_read") {
      msg_status = messaging::MessageStatus::read;
    } else {
      std::string const status_str = json.value("status", "");
      if (status_str == "delivered") {
        msg_status = messaging::MessageStatus::delivered;
      } else if (status_str == "read") {
        msg_status = messaging::MessageStatus::read;
      } else {
        return;
      }
    }

    std::vector<uint64_t> target_ids;
    if (json.contains("message_ids")) {
      for (auto const& id_val : json["message_ids"]) {
        target_ids.push_back(id_val.get<uint64_t>());
      }
    } else {
      target_ids.push_back(json.value("message_id", 0ULL));
    }

    for (auto const target_id : target_ids) {
      if (auto current_entry = history_->GetHistoryEntry(topic_id, target_id)) {
        if (current_entry->status == messaging::MessageStatus::read &&
            msg_status == messaging::MessageStatus::delivered) {
          continue;
        }

        if (is_mine) {
          history_->UpdateMessageStatus(topic_id, target_id, msg_status);
        } else {
          if (std::holds_alternative<messaging::GroupMessageEntry>(current_entry->data)) {
            history_->UpdateMessagePeerStatus(topic_id, target_id, entry.author_id, msg_status);

            bool all_read = true;
            bool all_delivered = true;
            auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()));
            if (group) {
              auto updated_entry = history_->GetHistoryEntry(topic_id, target_id);
              if (updated_entry) {
                if (auto* upd_group_entry = std::get_if<messaging::GroupMessageEntry>(&updated_entry->data)) {
                  for (auto const& member : group->members) {
                    if (member.contact_id == sm_->account->GetId()) continue;
                    messaging::MessageStatus member_status = messaging::MessageStatus::none;
                    for (auto const& ps : upd_group_entry->peer_statuses) {
                      if (ps.peer_id == member.contact_id) {
                        member_status = ps.status;
                        break;
                      }
                    }
                    if (member_status != messaging::MessageStatus::read) {
                      all_read = false;
                    }
                    if (member_status != messaging::MessageStatus::delivered &&
                        member_status != messaging::MessageStatus::read) {
                      all_delivered = false;
                    }
                  }
                }
              }
            }
            if (all_read) {
              history_->UpdateMessageStatus(topic_id, target_id, messaging::MessageStatus::read);
            } else if (all_delivered && current_entry->status != messaging::MessageStatus::read) {
              history_->UpdateMessageStatus(topic_id, target_id, messaging::MessageStatus::delivered);
            }
          } else {
            history_->UpdateMessageStatus(topic_id, target_id, msg_status);
          }
        }
      }
    }
  }

  void
  HandleChatProjection(TopicID const& topic_id, messaging::LedgerEntry const& entry, Json const& json, bool is_mine) {
    uint64_t message_id = json.value("message_id", 0ULL);

    if (history_->GetHistoryEntry(topic_id, message_id)) {
      return;
    }

    auto ledger_ts = static_cast<uint64_t>(entry.created_at.value);
    if (ledger_ts < 10000000000ULL) {
      ledger_ts *= Time::uS;
    } else if (ledger_ts < 10000000000000ULL) {
      ledger_ts *= Time::mS;
    }

    uint64_t msg_timestamp = json.value("timestamp", ledger_ts);

    bool const is_system = json.value("is_system", false);
    auto payload = json.value("payload", Json::object());
    std::string const content = payload.value(keyContent, "");
    uint64_t const reply_to_id = payload.value("reply_to_id", 0ULL);

    messaging::HistoryEntryKind const kind =
        is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage;

    std::vector<messaging::Attachment> attachments;
    if (json.contains("attachments")) {
      std::filesystem::path const storage_root(storage_->GetPath());
      std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
      std::error_code err;
      std::filesystem::create_directories(attachments_dir, err);

      for (auto const& j_att : json["attachments"]) {
        messaging::Attachment att;
        att.id = j_att.value("id", "");
        att.type = j_att.value("type", messaging::AttachmentType::file);
        att.mime_type = j_att.value("mime", "");
        att.size_bytes = j_att.value("size", 0ULL);
        att.thumbnail_base64 = j_att.value("thumb", "");
        att.hash = j_att.value("hash", "");
        att.note = j_att.value("note", "");
        att.file_name = j_att.value("name", "");

        att.is_downloaded = false;
        att.status = messaging::AttachmentStatus::pending;
        std::string filename = att.file_name;
        if (filename.empty()) {
          filename = att.id + GetExtensionForMimeType(att.mime_type);
        }
        att.file_path = ResolvePathConflict(attachments_dir, filename).string();
        attachments.push_back(att);
      }
    }

    auto db_entry = history_->AddTextEntry(
        {.topic_id = topic_id,
         .content = content,
         .attachments = attachments,
         .direction = is_mine ? messaging::MessageDirection::sent : messaging::MessageDirection::received,
         .kind = kind,
         .id = message_id,
         .reply_to_id = reply_to_id,
         .timestamp = msg_timestamp,
         .author_id = entry.author_id}
    );

    if (is_mine) {
      history_->UpdateMessageStatus(topic_id, message_id, messaging::MessageStatus::delivered);
      db_entry.status = messaging::MessageStatus::delivered;
    }

    for (auto const& att : attachments) {
      if (!att.is_downloaded && att.status != messaging::AttachmentStatus::discarded &&
          att.status != messaging::AttachmentStatus::canceled) {
        sm_->file_transfer->QueueDownload(entry.author_id, att, db_entry.id);
        sm_->file_transfer->ResumeTransfer(att.id, false);
      }
    }

    EventQueue::Instance().Push(EventChatMessageReceived{topic_id, db_entry});

    if (!is_mine) {
      QueueDeliveryReceipt(topic_id, message_id);

      if (!is_system) {
        bool should_evaluate_now = false;
        {
          std::lock_guard<std::mutex> const lock(sync_state_mutex_);
          if (!is_syncing_[ContactID(topic_id.Span())]) {
            should_evaluate_now = true;
          } else {
            unread_evaluation_needed_[ContactID(topic_id.Span())] = true;
          }
        }
        if (should_evaluate_now && !sm_->is_foreground) {
          bg_queue_.Dispatch([this, topic_id]() { EvaluateNotifications(topic_id, true, false); });
        }
      }
    }
  }

  auto ProcessIncomingAttachments(std::vector<messaging::Attachment> const& attachments)
      -> std::vector<messaging::Attachment> {
    std::vector<messaging::Attachment> local_attachments = attachments;
    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";

    if (!local_attachments.empty()) {
      std::error_code err;
      std::filesystem::create_directories(attachments_dir, err);
      for (auto& att : local_attachments) {
        std::string filename = att.file_name;
        if (filename.empty()) {
          filename = att.id + GetExtensionForMimeType(att.mime_type);
        }
        std::filesystem::path const final_path = ResolvePathConflict(attachments_dir, filename);
        att.file_path = final_path.string();
        att.status = messaging::AttachmentStatus::pending;
      }
    }
    return local_attachments;
  }

  void TriggerNewMessageNotificationIfNeeded(TopicID const& topic_id, bool is_system, uint64_t entry_id) {
    if (!is_system && !sm_->is_foreground) {
      auto contact = contact_manager_->GetContact(ContactID(topic_id.Span()));
      std::string const name =
          contact && contact->display_name ? *contact->display_name : topic_id.ToString().substr(0, 8);
      auto unread = GetUnreadMessages(topic_id);
      std::erase_if(unread, [](auto const& msg) -> bool {
        if (msg.kind == messaging::HistoryEntryKind::systemMessage) {
          return true;
        }
        if (std::holds_alternative<messaging::CallEntry>(msg.data)) {
          return std::get<messaging::CallEntry>(msg.data).call_status == messaging::CallStatus::missed;
        }
        return false;
      });
      if (!unread.empty()) {
        EventQueue::Instance().Push(EventNotifyNewMessage{topic_id, entry_id, name, std::move(unread)});
      }
    }
  }

  void EvaluateNotifications(TopicID const& topic_id, bool evaluate_unread, bool evaluate_missed_calls) {
    if (sm_->is_foreground) {
      return;
    }

    auto unread = GetUnreadMessages(topic_id);
    if (unread.empty()) {
      return;
    }

    std::string name = topic_id.ToString().substr(0, 8);
    if (auto contact = contact_manager_->GetContact(ContactID(topic_id.Span()))) {
      if (contact->display_name) name = *contact->display_name;
    } else if (auto group = sm_->group_manager->GetGroup(GroupID(topic_id.Span()))) {
      name = group->name;
    } else if (auto channel = sm_->channel_manager->GetChannel(ChannelID(topic_id.Span()))) {
      name = channel->name;
    }

    if (evaluate_missed_calls) {
      bool has_missed_call = false;
      for (auto it = unread.rbegin(); it != unread.rend(); ++it) {
        auto const& msg = *it;
        if (msg.kind == messaging::HistoryEntryKind::voiceCall || msg.kind == messaging::HistoryEntryKind::videoCall) {
          if (std::holds_alternative<messaging::CallEntry>(msg.data)) {
            if (std::get<messaging::CallEntry>(msg.data).call_status == messaging::CallStatus::missed) {
              has_missed_call = true;
              break;
            }
          }
        }
      }
      if (has_missed_call) {
        EventQueue::Instance().Push(EventNotifyMissedCall{ContactID(topic_id.Span()), name});
      }
    }

    if (evaluate_unread) {
      std::vector<messaging::HistoryEntryRecord> text_unread;
      text_unread.reserve(unread.size());
      for (auto& msg : unread) {
        if (msg.kind != messaging::HistoryEntryKind::systemMessage &&
            msg.kind != messaging::HistoryEntryKind::voiceCall && msg.kind != messaging::HistoryEntryKind::videoCall) {
          text_unread.push_back(std::move(msg));
        }
      }

      if (!text_unread.empty()) {
        auto latest_id = text_unread.back().id;
        EventQueue::Instance().Push(EventNotifyNewMessage{topic_id, latest_id, name, std::move(text_unread)});
      }
    }
  }

  static auto GenerateAttachmentId() -> std::string {
    static std::random_device random_device;
    static std::mt19937 gen(random_device());
    static std::uniform_int_distribution<uint64_t> dis;
    std::stringstream sstream;
    sstream << std::hex << dis(gen) << dis(gen);
    return sstream.str();
  }

  void FlushDeliveryReceipts(TopicID const& topic_id) {
    std::vector<uint64_t> ids;
    {
      std::lock_guard<std::mutex> const lock(sync_state_mutex_);
      if (is_syncing_[ContactID(topic_id.Span())]) {
        delivery_receipts_debounce_active_[ContactID(topic_id.Span())] = false;
        return;
      }
      ids = std::move(pending_delivery_receipts_[topic_id]);
      pending_delivery_receipts_.erase(topic_id);
      delivery_receipts_debounce_active_[ContactID(topic_id.Span())] = false;
    }
    if (ids.empty()) {
      return;
    }
    Json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_ids", ids}};
    sm_->sync_engine->AppendAction(topic_id, messaging::LedgerActionType::Message, receipt);
  }

  void QueueDeliveryReceipt(TopicID const& topic_id, uint64_t message_id) {
    std::lock_guard<std::mutex> const lock(sync_state_mutex_);
    pending_delivery_receipts_[topic_id].push_back(message_id);
    if (!delivery_receipts_debounce_active_[ContactID(topic_id.Span())]) {
      delivery_receipts_debounce_active_[ContactID(topic_id.Span())] = true;
      bg_queue_.DispatchAfter(std::chrono::seconds(1), [this, topic_id]() { FlushDeliveryReceipts(topic_id); });
    }
  }

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<messaging::MessageHistory> history_;

  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unordered_map<TopicID, std::vector<uint64_t>> pending_delivery_receipts_;
  std::unordered_map<ContactID, bool> delivery_receipts_debounce_active_;
  std::unordered_map<ContactID, bool> is_syncing_;
  std::unordered_map<ContactID, bool> unread_evaluation_needed_;
  std::unordered_map<ContactID, bool> missed_call_evaluation_needed_;
  std::mutex sync_state_mutex_;
  DispatchQueue bg_queue_{"MsgSvcBg", 1};
};

}  // namespace eillance::services
