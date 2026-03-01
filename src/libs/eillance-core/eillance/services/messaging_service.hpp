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
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "delivery_service.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

namespace {

constexpr size_t kMaxAttachmentNoteLength = 120;

auto GetExtensionForMimeType(std::string_view const mime_type) -> std::string {
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
    delivery_service_ = std::make_unique<messaging::DeliveryService>(sm_);

    auto sub1 = EventQueue::Instance().Subscribe<EventSendQueuedMessages>([this](auto const& e) {
      this->SendQueuedMessages(e.peer_id);
    });
    event_unsubs_.push_back([sub1]() { EventQueue::Instance().Unsubscribe<EventSendQueuedMessages>(sub1); });

    auto sub2 = EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      this->UnsuspendAndDeliver(e.peer_id);
    });
    event_unsubs_.push_back([sub2]() { EventQueue::Instance().Unsubscribe<EventDataChannelOpen>(sub2); });

    auto sub3 = EventQueue::Instance().Subscribe<EventPeerActivity>([this](auto const& e) {
      this->UnsuspendAndDeliver(e.peer_id);
    });
    event_unsubs_.push_back([sub3]() { EventQueue::Instance().Unsubscribe<EventPeerActivity>(sub3); });
  }

  ~MessagingService() {
    for (auto const& unsub : event_unsubs_) unsub();
    StopDeliveryService();
  }

  void StartDeliveryService() {
    delivery_service_->Start();
  }

  void StopDeliveryService() {
    delivery_service_->Stop();
  }

  auto SendChatMessage(
      ContactID const& peer_id,
      std::string const& message,
      std::vector<messaging::AttachmentRequest> const& attachments = {}
  ) -> messaging::HistoryEntryRecord {
    if (!contact_manager_->GetContact(peer_id)) {
      LGE << "Cannot send message to " << peer_id.ToString() << ". Not a contact.";
      return {};
    }

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

      std::string note = req.note;
      if (note.length() > kMaxAttachmentNoteLength) {
        note = note.substr(0, kMaxAttachmentNoteLength);
      }

      domain_attachments.push_back(
          {.id = GenerateAttachmentId(),
           .type = req.type,
           .file_path = req.file_path,
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
         .kind = messaging::HistoryEntryKind::textMessage}
    );

    nlohmann::json const action = {{"type", "chat"}, {"message_id", entry.id}};
    storage_->AddPendingAction(
        {.peer_id = peer_id, .payload = action.dump(), .timestamp = Time::Now(), .ref_id = entry.id}
    );

    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }

    EventQueue::Instance().Push(EventMessageUpdated{peer_id, entry.id});

    return entry;
  }

  void SendDeleteMessage(ContactID const& peer_id, std::uint64_t message_id) {
    history_->MarkMessageDeleted(peer_id, message_id);

    auto remote_id = history_->GetRemoteId(peer_id, message_id);
    uint64_t target_id = remote_id ? *remote_id : message_id;

    nlohmann::json const action = {{"type", "message_delete"}, {"target_id", target_id}};

    storage_->AddPendingAction({.peer_id = peer_id, .payload = action.dump(), .timestamp = Time::Now()});
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
    EventQueue::Instance().Push(EventMessageUpdated{peer_id, message_id});
  }

  void SendEditMessage(ContactID const& peer_id, std::uint64_t message_id, std::string const& new_content) {
    history_->UpdateMessageContent(peer_id, message_id, new_content);

    auto remote_id = history_->GetRemoteId(peer_id, message_id);
    uint64_t target_id = remote_id ? *remote_id : message_id;

    nlohmann::json const action = {{"type", "message_edit"}, {"target_id", target_id}, {"content", new_content}};

    storage_->AddPendingAction({.peer_id = peer_id, .payload = action.dump(), .timestamp = Time::Now()});
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
    EventQueue::Instance().Push(EventMessageUpdated{peer_id, message_id});
  }

  void SendReaction(ContactID const& peer_id, std::uint64_t message_id, messaging::Reaction reaction) {
    history_->ToggleReaction(peer_id, message_id, sm_->account->GetId(), reaction);

    auto remote_id = history_->GetRemoteId(peer_id, message_id);
    uint64_t target_id = remote_id ? *remote_id : message_id;

    nlohmann::json const action = {
        {"type", "message_reaction"},
        {"target_id", target_id},
        {"reaction", static_cast<int>(reaction)}
    };

    storage_->AddPendingAction({.peer_id = peer_id, .payload = action.dump(), .timestamp = Time::Now()});
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
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
      nlohmann::json const action_payload = {{"type", "chat"}, {"message_id", entry.id}};
      storage_->AddPendingAction(
          {.peer_id = peer_id, .payload = action_payload.dump(), .timestamp = Time::Now(), .ref_id = entry.id}
      );
    } else {
      if (auto const peer = transport_->GetPeer(peer_id);
          peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
        bool should_remove = false;
        ProcessChatAction(peer_id, entry.id, peer, should_remove);
      }
    }

    if (persist) {
      if (auto peer = transport_->GetPeer(peer_id);
          peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      } else {
        delivery_service_->AttemptDelivery(peer_id);
      }
    }
  }

  void UnsuspendAndDeliver(ContactID const& peer_id) {
    storage_->UnsuspendActionsForPeer(peer_id, Time::Now());
    EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
  }

  void SendQueuedMessages(ContactID const& peer_id) {
    {
      std::lock_guard<std::recursive_mutex> const lock(flushing_mutex_);
      if (flushing_peers_.contains(peer_id)) {
        return;
      }
      flushing_peers_.insert(peer_id);
    }

    struct ScopedFlushGuard {
      MessagingService& svc;
      ContactID pid;
      ~ScopedFlushGuard() {
        std::lock_guard<std::recursive_mutex> const lock(svc.flushing_mutex_);
        svc.flushing_peers_.erase(pid);
      }
    } guard{*this, peer_id};

    auto const peer = transport_->GetPeer(peer_id);

    auto pending_actions = storage_->GetPendingActions(peer_id);
    if (pending_actions.empty()) return;

    auto const now = Time::Now();

    for (auto const& action : pending_actions) {
      if (WasSentRecently(action.id, now)) {
        continue;
      }

      LGI << "Processing pending action " << action.id << " for " << peer_id.ToString();

      bool success = false;
      bool remove_on_success = true;

      try {
        auto json_action = nlohmann::json::parse(action.payload);
        std::string const type = json_action.value("type", "");

        if (type == "chat") {
          remove_on_success = false;
          uint64_t const msg_id = json_action.value("message_id", 0ULL);
          bool should_remove = false;
          success = ProcessChatAction(peer_id, msg_id, peer, should_remove);
          if (should_remove) {
            remove_on_success = true;
          }
        } else if (type == "contact_confirm") {
          transport_->SendSignalingMessage(json_action);
          success = true;
        } else {
          if (peer && peer->data_channel_is_open) {
            if (transport_->SendJson(peer_id, json_action)) {
              success = true;
            } else {
              peer->data_channel_is_open = false;
              break;
            }
          }
        }
      } catch (std::exception const& e) {
        LGE << "Failed to process pending action " << action.id << ": " << e.what();
        storage_->RemovePendingAction(action.id);
        continue;
      }

      if (success) {
        if (remove_on_success) {
          storage_->RemovePendingAction(action.id);
        } else {
          MarkAsSent(action.id, now);
        }
      }
    }

    EventQueue::Instance().Push(EventConnectionAttemptFinished{peer_id});
  }

  void CancelTransfer(
      ContactID const& peer_id,
      std::string const& attachment_id,
      bool const is_upload,
      bool const notify_peer = true
  ) const {
    sm_->file_transfer->CancelTransfer(peer_id, attachment_id, is_upload, notify_peer);

    if (is_upload) {
      auto sending_msgs = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
      for (auto const& entry : sending_msgs) {
        if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          continue;
        }
        auto const& [content, attachments] = std::get<messaging::MessageEntry>(entry.data);
        bool contains_attachment = false;
        bool has_other_content = !content.empty();
        for (auto const& att : attachments) {
          if (att.id == attachment_id) {
            contains_attachment = true;
          } else {
            if (auto stored = storage_->GetAttachment(att.id);
                stored && stored->status != messaging::AttachmentStatus::canceled &&
                stored->status != messaging::AttachmentStatus::discarded) {
              has_other_content = true;
            }
          }
        }
        if (contains_attachment && !has_other_content) {
          history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::none);
          storage_->RemovePendingActionByRefId(peer_id, entry.id);
          break;
        }
      }
    }
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool const is_upload) {
    sm_->file_transfer->ResumeTransfer(peer_id, attachment_id, is_upload);
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
    }
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
      storage_->AddPendingAction({.peer_id = peer_id, .payload = cancel_msg.dump()});
      if (auto const peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      }
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
        auto& [content, attachments] = std::get<messaging::MessageEntry>(entry.data);
        bool entry_modified = false;
        for (auto& att : attachments) {
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

  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids
  ) const -> std::vector<messaging::ChatSummary> {
    return history_->GetChatSummaries(peer_ids);
  }

  [[nodiscard]] auto GetUnreadMessages(ContactID const& peer_id) const -> std::vector<messaging::HistoryEntryRecord> {
    auto messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::delivered);
    std::erase_if(messages, [](auto const& msg) { return msg.direction != messaging::MessageDirection::received; });
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
    auto remote_ids = storage_->GetRemoteIds(peer_id, message_ids);
    if (remote_ids.empty()) {
      return;
    }
    nlohmann::json const msg = {{"type", "message_read"}, {"message_ids", remote_ids}};
    storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
    if (auto const peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
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
      std::vector<std::byte> const& encrypted_payload,
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
        if (h_entry.remote_id.has_value() && h_entry.remote_id.value() == message_id) {
          nlohmann::json const receipt = {
              {"type", "message_receipt"},
              {"status", "delivered"},
              {"message_id", message_id}
          };
          storage_->AddPendingAction({.peer_id = peer_id, .payload = receipt.dump()});
          EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
          return;
        }
      }
    }

    auto decrypted = peer->session_cipher->Decrypt({encrypted_payload.begin(), encrypted_payload.end()});
    if (decrypted.empty()) {
      LGE << "Failed to decrypt chat message from " << peer_id.ToString();
      return;
    }

    std::string content;
    try {
      auto json_payload = nlohmann::json::parse(decrypted);
      if (auto const received_version = json_payload.value(keyVersion, 0); received_version < minSupportedVersion) {
        throw std::runtime_error("Message version too old: " + std::to_string(received_version));
      }
      content = json_payload.at(keyContent).get<std::string>();
    } catch (std::exception const&) {
      nlohmann::json const error_json = {{"action", "unsupported_format"}};
      auto entry = history_->AddTextEntry(
          {.peer_id = peer_id,
           .content = error_json.dump(),
           .direction = messaging::MessageDirection::received,
           .kind = messaging::HistoryEntryKind::systemMessage,
           .remote_id = message_id}
      );
      nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      storage_->AddPendingAction({.peer_id = peer_id, .payload = receipt.dump()});
      SendQueuedMessages(peer_id);
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

    auto entry = history_->AddTextEntry(
        {.peer_id = peer_id,
         .content = content,
         .attachments = local_attachments,
         .direction = messaging::MessageDirection::received,
         .kind = is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage,
         .remote_id = message_id}
    );

    if (!local_attachments.empty()) {
      for (auto const& att : local_attachments) {
        sm_->file_transfer->QueueDownload(peer_id, att, entry.id);
      }
    }

    nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    storage_->AddPendingAction({.peer_id = peer_id, .payload = receipt.dump()});
    EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});

    EventQueue::Instance().Push(EventChatMessageReceived{peer_id, entry});
  }

  void OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status) const {
    auto message_status = messaging::MessageStatus::sending;
    if (status == "delivered") {
      message_status = messaging::MessageStatus::delivered;
    } else if (status == "read") {
      message_status = messaging::MessageStatus::read;
    } else {
      return;
    }
    history_->UpdateMessageStatus(peer_id, message_id, message_status);
    storage_->RemovePendingActionByRefId(peer_id, message_id);
    EventQueue::Instance().Push(EventMessageStatusChanged{peer_id, message_id, message_status});
  }

  void OnMessageDelete(ContactID const& peer_id, uint64_t target_id) {
    auto local_id = history_->FindMessageIdByRemoteOrLocal(peer_id, target_id);
    if (local_id) {
      history_->MarkMessageDeleted(peer_id, *local_id);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, *local_id});
    }
  }

  void OnMessageEdit(ContactID const& peer_id, uint64_t target_id, std::string const& new_content) {
    auto local_id = history_->FindMessageIdByRemoteOrLocal(peer_id, target_id);
    if (local_id) {
      history_->UpdateMessageContent(peer_id, *local_id, new_content);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, *local_id});
    }
  }

  void OnMessageReaction(ContactID const& peer_id, uint64_t target_id, messaging::Reaction reaction) {
    auto local_id = history_->FindMessageIdByRemoteOrLocal(peer_id, target_id);
    if (local_id) {
      history_->ToggleReaction(peer_id, *local_id, peer_id, reaction);
      EventQueue::Instance().Push(EventMessageUpdated{peer_id, *local_id});
    }
  }

  void OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) const {
    for (auto id : message_ids) {
      history_->UpdateMessageStatus(peer_id, id, messaging::MessageStatus::read);
      EventQueue::Instance().Push(EventMessageStatusChanged{peer_id, id, messaging::MessageStatus::read});
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

  bool ProcessChatAction(
      ContactID const& peer_id,
      uint64_t msg_id,
      std::shared_ptr<transport::Peer> const& peer,
      bool& remove_action
  ) {
    remove_action = false;

    auto messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
    std::optional<messaging::HistoryEntryRecord> entry;
    for (auto const& m : messages) {
      if (m.id == msg_id) {
        entry = m;
        break;
      }
    }

    if (!entry) {
      remove_action = true;
      return true;
    }

    if (!std::holds_alternative<messaging::MessageEntry>(entry->data)) {
      remove_action = true;
      return true;
    }

    auto const& [content, attachments] = std::get<messaging::MessageEntry>(entry->data);
    std::string content_to_send;

    if (entry->is_truncated) {
      if (auto full = history_->GetFullMessageContent(peer_id, entry->id)) {
        content_to_send = *full;
      } else {
        remove_action = true;
        return true;
      }
    } else {
      content_to_send = content;
    }

    if (entry->kind == messaging::HistoryEntryKind::systemMessage) {
      try {
        nlohmann::json content_json = nlohmann::json::parse(content_to_send);
        std::string const action = content_json.value("action", "");

        if (action == "contact_request" || action == "contact_confirm") {
          nlohmann::json msg = {
              {"type", action == "contact_request" ? "contact_add_request" : "contact_confirm"},
              {"to", peer_id.ToString()}
          };
          if (action == "contact_request") {
            if (content_json.contains("profile")) msg["profile"] = content_json["profile"];
            if (content_json.contains("token")) msg["token"] = content_json["token"];
          }
          transport_->SendSignalingMessage(msg);
          remove_action = true;
          return true;
        }
      } catch (...) {
      }
    }

    if (!peer || !peer->data_channel_is_open || !peer->session_cipher->IsReady()) {
      return false;
    }

    std::vector<messaging::Attachment> valid_attachments;
    for (auto const& att : attachments) {
      if (auto stored = storage_->GetAttachment(att.id)) {
        if (stored->status != messaging::AttachmentStatus::canceled &&
            stored->status != messaging::AttachmentStatus::discarded &&
            stored->status != messaging::AttachmentStatus::peerCanceled) {
          valid_attachments.push_back(*stored);
        }
      }
    }

    if (content_to_send.empty() && valid_attachments.empty()) {
      history_->UpdateMessageStatus(peer_id, entry->id, messaging::MessageStatus::none);
      remove_action = true;
      return true;
    }

    nlohmann::json payload_json;
    payload_json[keyVersion] = protocolVersion;
    payload_json[keyContent] = content_to_send;

    auto encrypted = peer->session_cipher->Encrypt(payload_json.dump());

    if (!encrypted.empty()) {
      nlohmann::json json_msg = {
          {"type", "chat"},
          {"message_id", entry->id},
          {"is_system", (entry->kind == messaging::HistoryEntryKind::systemMessage)},
          {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
      };

      if (!valid_attachments.empty()) {
        std::vector<nlohmann::json> att_list;
        att_list.reserve(valid_attachments.size());
        for (auto const& att : valid_attachments) {
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

      if (transport_->SendJson(peer_id, json_msg)) {
        if (!valid_attachments.empty()) {
          for (auto const& att : valid_attachments) {
            sm_->file_transfer->QueueUpload(peer_id, att);
          }
        }
        remove_action = false;
        return true;
      }

      peer->data_channel_is_open = false;
      return false;
    }

    return false;
  }

  void MarkAsSent(int64_t action_id, Time::Unit timestamp) {
    std::lock_guard<std::mutex> const lock(sent_cache_mutex_);
    sent_actions_cache_[action_id] = timestamp;

    if (sent_actions_cache_.size() > cachedActionsSize) {
      for (auto it = sent_actions_cache_.begin(); it != sent_actions_cache_.end();) {
        if (timestamp - it->second > debounceTime) {
          it = sent_actions_cache_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  auto WasSentRecently(int64_t action_id, Time::Unit current_time) -> bool {
    std::lock_guard<std::mutex> const lock(sent_cache_mutex_);
    if (sent_actions_cache_.contains(action_id)) {
      if (current_time - sent_actions_cache_[action_id] < debounceTime) {
        return true;
      }
      sent_actions_cache_.erase(action_id);
    }
    return false;
  }

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::unique_ptr<messaging::MessageHistory> history_;
  std::unique_ptr<messaging::DeliveryService> delivery_service_;

  std::vector<std::function<void()>> event_unsubs_;
  std::unordered_set<ContactID> flushing_peers_;
  std::recursive_mutex flushing_mutex_;

  std::unordered_map<int64_t, Time::Unit> sent_actions_cache_;
  std::mutex sent_cache_mutex_;

  static constexpr std::size_t cachedActionsSize = 100;
  static constexpr Time::Unit debounceTime = 2000000;  // 2 seconds
};

}  // namespace eillance::services
