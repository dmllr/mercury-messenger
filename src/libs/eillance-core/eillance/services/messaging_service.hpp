#pragma once

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

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
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

struct AttachmentRequest {
  std::string file_path;
  messaging::AttachmentType type;
  std::string thumbnail_base64;
  std::string mime_type;
  std::string note;
  std::string file_name;
};

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

  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  explicit MessagingService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      std::shared_ptr<FileTransferService> file_transfer_service,
      ConnectionRequestHandler connection_handler
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)),
        contact_manager_(std::move(contact_manager)),
        file_transfer_service_(std::move(file_transfer_service)),
        connection_handler_(std::move(connection_handler)) {
    history_ = std::make_unique<messaging::MessageHistory>(storage_);

    history_->SetStatusChangedCallback(
        [this](ContactID const& peer_id, uint64_t msg_id, messaging::MessageStatus status) {
          INVARIANT(on_status_changed_);
          on_status_changed_(peer_id, msg_id, status);
        }
    );

    delivery_service_ = std::make_unique<messaging::MessageDeliveryService>(
        storage_,
        contact_manager_,
        [this](ContactID const& peer_id) { RequestConnection(peer_id); },
        [this](ContactID const& peer_id) { return HasPendingWork(peer_id); },
        [this](ContactID const& peer_id) {
          if (auto p = transport_->GetPeer(peer_id)) {
            return p->data_channel_is_open.load();
          }
          return false;
        }
    );
  }

  void StartDeliveryService() {
    delivery_service_->Start();
  }

  void StopDeliveryService() {
    delivery_service_->Stop();
  }

  // Sending
  auto SendChatMessage(
      ContactID const& peer_id,
      std::string const& message,
      std::vector<AttachmentRequest> const& attachments = {}
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

      try {
        file_size = std::filesystem::file_size(req.file_path);
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
        peer_id,
        message,
        domain_attachments,
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::textMessage
    );

    // Queue action for unified delivery
    nlohmann::json action = {{"type", "chat"}, {"message_id", entry.id}};
    storage_->AddPendingAction(peer_id, action.dump(), Time::Now(), entry.id);

    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }

    return entry;
  }

  void SendSystemMessage(ContactID const& peer_id, std::string const& content) {
    auto const entry = history_->AddTextEntry(
        peer_id,
        content,
        {},
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );

    INVARIANT(on_message_received_);
    on_message_received_(peer_id, entry);

    nlohmann::json const action_payload = {{"type", "chat"}, {"message_id", entry.id}};
    storage_->AddPendingAction(peer_id, action_payload.dump(), Time::Now(), entry.id);

    if (auto peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
  }

  void UnsuspendAndDeliver(ContactID const& peer_id) {
    // Only log if suspensions were actually processed
    storage_->UnsuspendActionsForPeer(peer_id, Time::Now());
    SendQueuedMessages(peer_id);
  }

  // Attempts to flush queued messages
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

    file_transfer_service_->OnConnectionReady(peer_id);

    auto pending_actions = storage_->GetPendingActions(peer_id);
    if (pending_actions.empty()) return;

    auto const now = Time::Now();

    for (auto const& action : pending_actions) {
      // Debounce check: if this action ID was successfully sent recently, skip it.
      if (WasSentRecently(action.id, now)) {
        continue;
      }

      // Log only if we are actually processing it
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
        } else {
          // Generic actions (Receipts, Profile, etc.)
          if (peer && peer->data_channel_is_open) {
            if (transport_->SendPeerDataChannelMessage(peer_id, json_action)) {
              success = true;
            } else {
              // Sending failed: likely DataChannel state mismatch
              peer->data_channel_is_open = false;
              LGI << "Failed to send action via DataChannel to " << peer_id.ToString() << ". Marking as closed.";
              // Break loop to prevent further failures and avoid tight retry loops if transport is dead
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
          // If we are not removing it (e.g. waiting for receipt), mark it as sent recently
          // so we don't spam the network on the next immediate flush call.
          MarkAsSent(action.id, now);
        }
      }
    }

    delivery_service_->ConnectionAttemptFinished(peer_id);
  }

  void OnConnectionAttemptFinished(ContactID const& peer_id) const {
    delivery_service_->ConnectionAttemptFinished(peer_id);
  }

  void CancelTransfer(
      ContactID const& peer_id,
      std::string const& attachment_id,
      bool const is_upload,
      bool const notify_peer = true
  ) const {
    file_transfer_service_->CancelTransfer(peer_id, attachment_id, is_upload, notify_peer);
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
    RequestConnection(peer_id);
    file_transfer_service_->ResumeTransfer(peer_id, attachment_id, is_upload);
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) {
    auto const attachment = storage_->GetAttachment(attachment_id);
    bool should_send_cancel = true;
    if (attachment) {
      if (attachment->status == messaging::AttachmentStatus::canceled ||
          attachment->status == messaging::AttachmentStatus::peerCanceled ||
          attachment->status == messaging::AttachmentStatus::failed) {
        should_send_cancel = false;
      }
    }

    file_transfer_service_->DiscardAttachment(peer_id, attachment_id);

    if (should_send_cancel) {
      nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"file_id", attachment_id}};
      storage_->AddPendingAction(peer_id, cancel_msg.dump());
      if (auto const peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        SendQueuedMessages(peer_id);
      }
    }
  }

  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t const page_size) const
  -> messaging::HistoryPage {
    auto page = history_->GetHistoryPage(peer_id, page_index, page_size);
    std::erase_if(page.messages, [](auto const& entry) {
      return entry.status == messaging::MessageStatus::none && entry.kind != messaging::HistoryEntryKind::systemMessage;
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

  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) {
    history_->MarkMessagesAsRead(peer_id, message_ids);
    auto remote_ids = storage_->GetRemoteIds(peer_id, message_ids);
    if (remote_ids.empty()) {
      return;
    }
    nlohmann::json const msg = {{"type", "message_read"}, {"message_ids", remote_ids}};
    storage_->AddPendingAction(peer_id, msg.dump());
    if (auto const peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
  }

  void
  AddSystemEntry(ContactID const& peer_id, std::string const& content, messaging::MessageDirection const direction) {
    auto const entry =
        history_->AddTextEntry(peer_id, content, {}, direction, messaging::HistoryEntryKind::systemMessage);

    INVARIANT(on_message_received_);
    on_message_received_(peer_id, entry);
  }

  void AddCallEntry(
      ContactID const& peer_id,
      messaging::HistoryEntryKind const kind,
      messaging::CallDirection const direction,
      messaging::CallStatus const status,
      uint32_t const duration
  ) {
    history_->AddCallEntry(peer_id, kind, direction, status, duration);
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
      LGI << "Received message from unknown peer " << peer_id.ToString() << ". Auto-restoring contact.";
      contact_manager_->MarkAsConfirmed(peer_id);
      if (on_contact_restored_) {
        on_contact_restored_(peer_id);
      }
    }
    UpdatePeerActivity(peer_id);

    // Deduplication check: Do not re-add message if we already have it.
    // Just send the receipt again to confirm delivery to the sender.
    {
      constexpr auto page_size = 50U;  // TODO align with system-wide page size
      auto [messages, total_messages] = history_->GetHistoryPage(peer_id, 0, page_size);
      for (auto const& h_entry : messages) {
        if (h_entry.remote_id.has_value() && h_entry.remote_id.value() == message_id) {
          LGI << "Duplicate message received from " << peer_id.ToString() << " (ID: " << message_id
              << "). Sending receipt and ignoring.";
          nlohmann::json const receipt = {
              {"type", "message_receipt"},
              {"status", "delivered"},
              {"message_id", message_id}
          };
          storage_->AddPendingAction(peer_id, receipt.dump());
          SendQueuedMessages(peer_id);
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
    } catch (std::exception const& e) {
      LGE << "Failed to parse message payload from " << peer_id.ToString() << ": " << e.what();
      nlohmann::json const error_json = {{"action", "unsupported_format"}};
      auto entry = history_->AddTextEntry(
          peer_id,
          error_json.dump(),
          {},
          messaging::MessageDirection::received,
          messaging::HistoryEntryKind::systemMessage,
          std::make_optional(message_id)
      );
      nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      storage_->AddPendingAction(peer_id, receipt.dump());
      SendQueuedMessages(peer_id);
      INVARIANT(on_message_received_);
      on_message_received_(peer_id, entry);
      return;
    }

    std::vector<messaging::Attachment> local_attachments = attachments;
    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";

    if (!local_attachments.empty()) {
      std::error_code err;
      std::filesystem::create_directories(attachments_dir, err);
      if (err) {
        LGE << "Failed to create attachments directory: " << attachments_dir.string() << " Error: " << err.message();
      }

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
        peer_id,
        content,
        local_attachments,
        messaging::MessageDirection::received,
        is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage,
        std::make_optional(message_id)
    );

    if (!local_attachments.empty()) {
      for (auto const& att : local_attachments) {
        file_transfer_service_->QueueDownload(peer_id, att, entry.id);
      }
    }

    nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    storage_->AddPendingAction(peer_id, receipt.dump());
    SendQueuedMessages(peer_id);

    INVARIANT(on_message_received_);
    on_message_received_(peer_id, entry);
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
  }

  void OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) const {
    for (auto id : message_ids) {
      history_->UpdateMessageStatus(peer_id, id, messaging::MessageStatus::read);
    }
  }

  using OnChatMessageCallback = std::function<void(ContactID const& peer_id, messaging::HistoryEntryRecord const&)>;
  using OnMessageStatusChangedCallback = std::function<void(ContactID const&, uint64_t, messaging::MessageStatus)>;
  using OnAttachmentDownloadedCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id)>;
  using OnContactRestoredCallback = std::function<void(ContactID const&)>;
  using OnTransferProgressCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id, float progress)>;
  using OnAttachmentStatusChangedCallback = std::function<
      void(ContactID const& peer_id, std::string const& attachment_id, messaging::AttachmentStatus status)>;

  void SetChatMessageReceivedCallback(OnChatMessageCallback cb) {
    on_message_received_ = std::move(cb);
  }
  void SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb) {
    on_status_changed_ = std::move(cb);
  }
  void SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb) {
    file_transfer_service_->SetAttachmentDownloadedCallback(std::move(cb));
  }
  void SetContactRestoredCallback(OnContactRestoredCallback cb) {
    on_contact_restored_ = std::move(cb);
  }
  void SetTransferProgressCallback(OnTransferProgressCallback cb) {
    file_transfer_service_->SetTransferProgressCallback(std::move(cb));
  }
  void SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb) {
    file_transfer_service_->SetAttachmentStatusChangedCallback(std::move(cb));
  }

private:
  void RequestConnection(ContactID const& peer_id) {
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      connection_handler_(peer_id);
    }
  }
  void UpdatePeerActivity(ContactID const& peer_id) {
    if (auto const peer = transport_->GetPeer(peer_id)) {
      peer->last_activity_time = Time::Now();
    }
  }
  [[nodiscard]] auto HasPendingWork(ContactID const& peer_id) const -> bool {
    if (!storage_->GetPendingActions(peer_id).empty()) {
      return true;
    }
    auto const attachments = storage_->GetPendingAttachments(peer_id);
    return std::ranges::any_of(attachments, [](auto const& att) {
      return att.status == messaging::AttachmentStatus::pending ||
             att.status == messaging::AttachmentStatus::transferring;
    });
  }
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

    if (entry->kind != messaging::HistoryEntryKind::textMessage &&
        entry->kind != messaging::HistoryEntryKind::systemMessage) {
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
    UpdatePeerActivity(peer_id);

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

      if (transport_->SendPeerDataChannelMessage(peer_id, json_msg)) {
        if (!valid_attachments.empty()) {
          for (auto const& att : valid_attachments) {
            file_transfer_service_->QueueUpload(peer_id, att);
          }
        }
        remove_action = false;  // Keep pending until receipt
        return true;
      }

      // Send failed (DC closed). Do not remove action.
      peer->data_channel_is_open = false;
      return false;
    }

    return false;
  }

  void MarkAsSent(int64_t action_id, Time::Unit timestamp) {
    std::lock_guard<std::mutex> const lock(sent_cache_mutex_);
    sent_actions_cache_[action_id] = timestamp;

    // Cleanup old entries periodically (every 100 insertions/updates)
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

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  std::shared_ptr<FileTransferService> file_transfer_service_;
  ConnectionRequestHandler connection_handler_;
  std::unique_ptr<messaging::MessageHistory> history_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;
  OnChatMessageCallback on_message_received_;
  OnMessageStatusChangedCallback on_status_changed_;
  OnContactRestoredCallback on_contact_restored_;

  // Guard to prevent concurrent processing of the queue for the same peer
  std::unordered_set<ContactID> flushing_peers_;
  std::recursive_mutex flushing_mutex_;

  // Debouncing cache for sent actions
  std::unordered_map<int64_t, Time::Unit> sent_actions_cache_;
  std::mutex sent_cache_mutex_;

  static constexpr std::size_t cachedActionsSize = 100;
  static constexpr Time::Unit debounceTime = 2000000; // 2 seconds
};

}  // namespace eillance::services
