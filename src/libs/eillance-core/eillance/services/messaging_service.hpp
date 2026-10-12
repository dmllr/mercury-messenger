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
#include "eillance/identity/contact_manager.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

namespace {

constexpr size_t kMaxAttachmentNoteLength = 120;

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
 public:
  static constexpr std::uint16_t protocolVersion = 2;
  static constexpr std::uint16_t minSupportedVersion = 1;
  static constexpr auto keyVersion = "v";
  static constexpr auto keyContent = "c";

  struct Serializer {
    static auto BuildE2EEPayload(std::string const& content) -> std::string {
      nlohmann::json payload_json;
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
  }

  ~MessagingService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  auto SendChatMessage(
      ContactID const& peer_id,
      std::string const& message,
      std::vector<messaging::AttachmentRequest> const& attachments = {},
      std::uint64_t reply_to_id = 0
  ) -> messaging::HistoryEntryRecord {
    if (!contact_manager_->GetContact(peer_id)) {
      LGE << "Cannot send message to " << peer_id.ToString() << ". Not a contact.";
      return {};
    }

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
      if (note.length() > kMaxAttachmentNoteLength) {
        note = note.substr(0, kMaxAttachmentNoteLength);
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

    auto entry = history_->AddTextEntry(
        {.peer_id = peer_id,
         .content = message,
         .attachments = domain_attachments,
         .direction = messaging::MessageDirection::sent,
         .kind = messaging::HistoryEntryKind::textMessage,
         .reply_to_id = reply_to_id}
    );

    nlohmann::json payload_json;
    payload_json[keyVersion] = protocolVersion;
    payload_json[keyContent] = message;
    if (reply_to_id > 0) {
      payload_json["reply_to_id"] = reply_to_id;
    }

    nlohmann::json json_msg = {
        {"type", "chat"},
        {"message_id", entry.id},
        {"timestamp", entry.timestamp},
        {"is_system", false},
        {"payload", payload_json}
    };

    if (!domain_attachments.empty()) {
      std::vector<nlohmann::json> att_list;
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

    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, json_msg);

    for (auto const& att : domain_attachments) {
      sm_->file_transfer->QueueUpload(peer_id, att);
    }

    EventQueue::Instance().Push(EventMessageUpdated{peer_id, entry.id});

    return entry;
  }

  void SendDeleteMessage(ContactID const& peer_id, std::uint64_t message_id) {
    history_->MarkMessageDeleted(peer_id, message_id);

    nlohmann::json const action = {{"type", "message_delete"}, {"target_id", message_id}};

    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::MessageDelete, action);
    EventQueue::Instance().Push(EventMessageUpdated{peer_id, message_id});
  }

  void SendEditMessage(ContactID const& peer_id, std::uint64_t message_id, std::string const& new_content) {
    history_->UpdateMessageContent(peer_id, message_id, new_content);

    nlohmann::json const action = {{"type", "message_edit"}, {"target_id", message_id}, {"content", new_content}};

    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::MessageEdit, action);
    EventQueue::Instance().Push(EventMessageUpdated{peer_id, message_id});
  }

  void SendReaction(ContactID const& peer_id, std::uint64_t message_id, messaging::Reaction reaction) {
    history_->ToggleReaction(peer_id, message_id, sm_->account->GetId(), reaction);

    nlohmann::json const action = {
        {"type", "message_reaction"},
        {"target_id", message_id},
        {"reaction", static_cast<int>(reaction)}
    };

    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Reaction, action);
    EventQueue::Instance().Push(EventMessageUpdated{peer_id, message_id});
  }

  void SendSystemMessage(ContactID const& peer_id, std::string const& content, bool persist) {
    auto const entry = history_->AddTextEntry(
        {.peer_id = peer_id,
         .content = content,
         .direction = messaging::MessageDirection::sent,
         .kind = messaging::HistoryEntryKind::systemMessage}
    );

    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, entry});

    if (persist) {
      nlohmann::json const action_payload = {
          {"type", "chat"},
          {"message_id", entry.id},
          {"timestamp", entry.timestamp},
          {"is_system", true},
          {"payload", {{"c", content}, {"v", protocolVersion}}}
      };
      sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, action_payload);
    }
  }

  void CancelTransfer(
      ContactID const& peer_id,
      std::string const& attachment_id,
      bool const is_upload,
      bool const notify_peer = true
  ) const {
    sm_->file_transfer->CancelTransfer(peer_id, attachment_id, is_upload, notify_peer);

    if (is_upload) {
      auto const sending_msgs = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
      for (auto const& entry : sending_msgs) {
        if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          continue;
        }
        auto const& message = std::get<messaging::MessageEntry>(entry.data);
        bool contains_attachment = false;
        bool has_other_content = !message.content.empty();
        for (auto const& att : message.attachments) {
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
          history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::none);
          break;
        }
      }
    }
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool const is_upload) {
    sm_->file_transfer->ResumeTransfer(peer_id, attachment_id, is_upload);
    sm_->sync_engine->ProposeSync(TopicID(peer_id), peer_id);
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) {
    sm_->file_transfer->DiscardAttachment(peer_id, attachment_id);
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
      nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"file_id", attachment_id}};
      sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, cancel_msg);
    }
  }

  [[nodiscard]] auto GetDownloadedAttachments(ContactID const& peer_id) const
      -> std::vector<messaging::AttachmentRecord> {
    return storage_->GetDownloadedAttachments(peer_id);
  }

  void DeleteAttachmentLocal(ContactID const& peer_id, std::string const& attachment_id) const {
    auto att = storage_->GetAttachment(attachment_id);
    if (att && att->is_downloaded) {
      std::error_code ec;
      std::filesystem::remove(att->file_path, ec);
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::discarded, false);
      EventQueue::Instance().Push(
          EventAttachmentStatusChanged{peer_id, attachment_id, messaging::AttachmentStatus::discarded}
      );
    }
  }

  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t const page_size) const
      -> messaging::HistoryPage {
    auto page = history_->GetHistoryPage(peer_id, page_index, page_size);
    std::erase_if(page.messages, [](auto const& entry) {
      if (entry.status == messaging::MessageStatus::none && entry.kind != messaging::HistoryEntryKind::systemMessage &&
          entry.kind != messaging::HistoryEntryKind::voiceCall &&
          entry.kind != messaging::HistoryEntryKind::videoCall) {
        return true;
      }
      if (entry.kind == messaging::HistoryEntryKind::systemMessage &&
          std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto const& content = std::get<messaging::MessageEntry>(entry.data).content;
        if (content.find("\"action\":\"call_request\"") != std::string::npos ||
            content.find("\"action\":\"call_accept\"") != std::string::npos) {
          return true;
        }
      }
      return false;
    });

    for (auto& entry : page.messages) {
      if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto& message = std::get<messaging::MessageEntry>(entry.data);
        bool entry_modified = false;
        for (auto& att : message.attachments) {
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

  [[nodiscard]] auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id) const
      -> std::optional<std::string> {
    return history_->GetFullMessageContent(peer_id, message_id);
  }

  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids) const
      -> std::vector<messaging::ChatSummary> {
    return history_->GetChatSummaries(peer_ids);
  }

  [[nodiscard]] auto GetUnreadMessages(ContactID const& peer_id) const -> std::vector<messaging::HistoryEntryRecord> {
    auto messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::delivered);
    std::erase_if(messages, [](auto const& msg) -> bool {
      return msg.direction != messaging::MessageDirection::received;
    });
    std::ranges::sort(messages, {}, &messaging::HistoryEntryRecord::timestamp);
    return messages;
  }

  [[nodiscard]] auto SearchMessages(ContactID const& peer_id, std::string const& query) const
      -> std::vector<messaging::HistoryEntryRecord> {
    return history_->SearchMessages(peer_id, query);
  }

  [[nodiscard]] auto SearchAllMessages(std::string const& query) const -> std::vector<messaging::HistoryEntryRecord> {
    return history_->SearchAllMessages(query);
  }

  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) {
    history_->MarkMessagesAsRead(peer_id, message_ids);
    if (message_ids.empty()) {
      return;
    }
    nlohmann::json const msg = {{"type", "message_read"}, {"message_ids", message_ids}};
    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, msg);
  }

  // --- Read Model Projections ---
  void ProjectMessage(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) {
      return;
    }

    ContactID const peer_id(entry.topic_id.value.Span());

    if (!contact_manager_->GetContact(peer_id)) {
      contact_manager_->MarkAsConfirmed(peer_id);
      EventQueue::Instance().Push(EventContactRestored{peer_id});
    }

    std::string const type = json.value("type", "");

    if (type == "message_read") {
      auto ids = json.value("message_ids", std::vector<uint64_t>{});
      for (auto target_id : ids) {
        history_->UpdateMessageStatus(peer_id, target_id, messaging::MessageStatus::read);
      }
      return;
    }

    if (type == "message_receipt") {
      std::string const status_str = json.value("status", "");
      uint64_t const target_id = json.value("message_id", 0ULL);
      auto msg_status = messaging::MessageStatus::sending;
      if (status_str == "delivered") {
        msg_status = messaging::MessageStatus::delivered;
      } else if (status_str == "read") {
        msg_status = messaging::MessageStatus::read;
      } else {
        return;
      }

      if (history_->GetHistoryEntry(peer_id, target_id)) {
        history_->UpdateMessageStatus(peer_id, target_id, msg_status);
      }
      return;
    }

    if (type == "file_cancel") {
      sm_->file_transfer->HandleCancel(peer_id, json.value("file_id", ""));
      return;
    }

    if (type != "chat") {
      return;  // Unsupported message type
    }

    uint64_t message_id = json.value("message_id", 0ULL);

    auto ledger_ts = static_cast<uint64_t>(entry.created_at.value);
    if (ledger_ts < 10000000000ULL) {
      ledger_ts *= 1000000ULL;  // Convert seconds to microseconds
    } else if (ledger_ts < 10000000000000ULL) {
      ledger_ts *= 1000ULL;  // Convert milliseconds to microseconds
    }

    uint64_t msg_timestamp = json.value("timestamp", ledger_ts);

    bool const is_system = json.value("is_system", false);
    auto payload = json.value("payload", nlohmann::json::object());
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
        {.peer_id = peer_id,
         .content = content,
         .attachments = attachments,
         .direction = messaging::MessageDirection::received,
         .kind = kind,
         .id = message_id,
         .reply_to_id = reply_to_id,
         .timestamp = msg_timestamp}
    );

    for (auto const& att : attachments) {
      if (!att.is_downloaded && att.status != messaging::AttachmentStatus::discarded &&
          att.status != messaging::AttachmentStatus::canceled) {
        sm_->file_transfer->QueueDownload(peer_id, att, db_entry.id);
        sm_->file_transfer->ResumeTransfer(peer_id, att.id, false);
      }
    }

    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, db_entry});

    nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, receipt);

    if (!is_system && !sm_->is_foreground) {
      auto contact = contact_manager_->GetContact(peer_id);
      std::string const name =
          contact && contact->display_name ? *contact->display_name : peer_id.ToString().substr(0, 8);
      if (auto unread = GetUnreadMessages(peer_id); !unread.empty()) {
        EventQueue::Instance().Push(EventNotifyNewMessage{peer_id, db_entry.id, name, std::move(unread)});
      }
    }
  }

  void ProjectEdit(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) {
      return;
    }
    ContactID const peer_id(entry.topic_id.value.Span());
    uint64_t const target_id = json.value("target_id", 0ULL);
    std::string const content = json.value("content", "");
    if (history_->GetHistoryEntry(peer_id, target_id)) {
      history_->UpdateMessageContent(peer_id, target_id, content);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, target_id});
    }
  }

  void ProjectDelete(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) {
      return;
    }
    ContactID const peer_id(entry.topic_id.value.Span());
    uint64_t const target_id = json.value("target_id", 0ULL);
    if (history_->GetHistoryEntry(peer_id, target_id)) {
      history_->MarkMessageDeleted(peer_id, target_id);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, target_id});
    }
  }

  void ProjectReaction(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) {
      return;
    }
    ContactID const peer_id(entry.topic_id.value.Span());
    uint64_t const target_id = json.value("target_id", 0ULL);
    auto const reaction = static_cast<messaging::Reaction>(json.value("reaction", 0));
    if (history_->GetHistoryEntry(peer_id, target_id)) {
      history_->ToggleReaction(peer_id, target_id, entry.author_id, reaction);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, target_id});
    }
  }

  void ProjectCallLog(storage::LedgerEntry const& entry, nlohmann::json const& json, bool is_mine) {
    if (is_mine) {
      return;
    }
    ContactID const peer_id(entry.topic_id.value.Span());

    auto ledger_ts = static_cast<uint64_t>(entry.created_at.value);
    if (ledger_ts < 10000000000ULL) {
      ledger_ts *= 1000000ULL;
    } else if (ledger_ts < 10000000000000ULL) {
      ledger_ts *= 1000ULL;
    }

    uint64_t msg_timestamp = json.value("timestamp", ledger_ts);

    // Simple deduplication for recent call logs to prevent double-entry for answered calls
    auto [messages, total_messages] = history_->GetHistoryPage(peer_id, 0, 10);
    for (auto const& msg : messages) {
      if (msg.kind == messaging::HistoryEntryKind::voiceCall || msg.kind == messaging::HistoryEntryKind::videoCall) {
        if (abs(static_cast<std::int64_t>(msg.timestamp - msg_timestamp)) < 30000000ULL) {
          return;  // Duplicate
        }
      }
    }

    messaging::CallEntryParams const params{
        .peer_id = peer_id,
        .kind = json.value("is_video", false) ? messaging::HistoryEntryKind::videoCall
                                              : messaging::HistoryEntryKind::voiceCall,
        .direction = messaging::CallDirection::incoming,
        .status = static_cast<messaging::CallStatus>(json.value("call_status", 0)),
        .duration_seconds = json.value("duration_seconds", 0U),
        .id = json.value("message_id", 0ULL),
        .timestamp = msg_timestamp
    };
    auto const db_entry = history_->AddCallEntry(params);
    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, db_entry});

    if (params.status == messaging::CallStatus::missed && !sm_->is_foreground) {
      auto const contact = contact_manager_->GetContact(peer_id);
      std::string const name =
          contact && contact->display_name ? *contact->display_name : peer_id.ToString().substr(0, 8);
      EventQueue::Instance().Push(EventNotifyMissedCall{peer_id, name});
    }
  }

  void AddCallEntry(messaging::CallEntryParams const& params, bool sync_to_ledger) {
    auto const entry = history_->AddCallEntry(params);
    if (sync_to_ledger) {
      nlohmann::json const payload = {
          {"is_video", params.kind == messaging::HistoryEntryKind::videoCall},
          {"duration_seconds", params.duration_seconds},
          {"call_status", static_cast<int>(params.status)},
          {"timestamp", entry.timestamp}
      };
      sm_->sync_engine->AppendAction(TopicID(params.peer_id), storage::LedgerActionType::CallLog, payload);
    }
    EventQueue::Instance().Push(EventChatMessageReceived{params.peer_id, entry});
  }

  void
  AddSystemEntry(ContactID const& peer_id, std::string const& content, messaging::MessageDirection const direction) {
    auto const entry = history_->AddTextEntry(
        {.peer_id = peer_id,
         .content = content,
         .direction = direction,
         .kind = messaging::HistoryEntryKind::systemMessage}
    );
    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, entry});
  }

  void AddCallEntry(messaging::CallEntryParams const& params) {
    auto const entry = history_->AddCallEntry(params);
    EventQueue::Instance().Push(EventChatMessageReceived{params.peer_id, entry});
  }

  void OnChatMessageReceived(
      ContactID const& peer_id,
      nlohmann::json const& payload,
      uint64_t message_id,
      bool is_system,
      std::vector<messaging::Attachment> const& attachments = {}
  ) {
    auto peer = transport_->GetPeer(peer_id);
    if (!peer || !peer->session_cipher->IsReady()) {
      return;
    }

    if (!contact_manager_->GetContact(peer_id)) {
      contact_manager_->MarkAsConfirmed(peer_id);
      EventQueue::Instance().Push(EventContactRestored{peer_id});
    }

    if (auto const p = transport_->GetPeer(peer_id)) {
      p->last_activity_time = Time::Now();
    }

    {
      constexpr auto page_size = 50U;
      auto [messages, total_messages] = history_->GetHistoryPage(peer_id, 0, page_size);
      for (auto const& h_entry : messages) {
        if (h_entry.id == message_id) {
          nlohmann::json const receipt = {
              {"type", "message_receipt"},
              {"status", "delivered"},
              {"message_id", message_id}
          };
          sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, receipt);
          return;
        }
      }
    }

    std::string content;
    try {
      if (auto const received_version = payload.value(keyVersion, 0); received_version < minSupportedVersion) {
        throw std::runtime_error("Message version too old: " + std::to_string(received_version));
      }
      content = payload.at(keyContent).get<std::string>();
    } catch (std::exception const&) {
      nlohmann::json const error_json = {{"action", "unsupported_format"}};
      auto entry = history_->AddTextEntry(
          {.peer_id = peer_id,
           .content = error_json.dump(),
           .direction = messaging::MessageDirection::received,
           .kind = messaging::HistoryEntryKind::systemMessage}
      );
      nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, receipt);
      EventQueue::Instance().Push(EventChatMessageReceived{peer_id, entry});
      return;
    }

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

    uint64_t local_reply_to_id = 0;
    try {
      if (payload.contains("reply_to_id")) {
        local_reply_to_id = payload["reply_to_id"].get<uint64_t>();
      }
    } catch (...) {
    }

    auto entry = history_->AddTextEntry(
        {.peer_id = peer_id,
         .content = content,
         .attachments = local_attachments,
         .direction = messaging::MessageDirection::received,
         .kind = is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage,
         .id = message_id,
         .reply_to_id = local_reply_to_id}
    );

    if (!local_attachments.empty()) {
      for (auto const& att : local_attachments) {
        sm_->file_transfer->QueueDownload(peer_id, att, entry.id);
      }
    }

    nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    sm_->sync_engine->AppendAction(TopicID(peer_id), storage::LedgerActionType::Message, receipt);

    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, entry});
    if (!is_system && !sm_->is_foreground) {
      auto contact = contact_manager_->GetContact(peer_id);
      std::string const name =
          contact && contact->display_name ? *contact->display_name : peer_id.ToString().substr(0, 8);
      auto unread = GetUnreadMessages(peer_id);
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
        EventQueue::Instance().Push(EventNotifyNewMessage{peer_id, entry.id, name, std::move(unread)});
      }
    }
  }

 private:
  static auto GenerateAttachmentId() -> std::string {
    static std::random_device random_device;
    static std::mt19937 gen(random_device());
    static std::uniform_int_distribution<uint64_t> dis;
    std::stringstream sstream;
    sstream << std::hex << dis(gen) << dis(gen);
    return sstream.str();
  }

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<messaging::MessageHistory> history_;

  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
};

}  // namespace eillance::services
