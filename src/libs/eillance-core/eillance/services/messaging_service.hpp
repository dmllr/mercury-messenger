#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <openssl/sha.h>

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
inline auto GetExtensionForMimeType(std::string_view const mime_type) -> std::string {
  if (mime_type == "image/jpeg" || mime_type == "image/jpg") {
    return ".jpg";
  }
  if (mime_type == "image/png") {
    return ".png";
  }
  if (mime_type == "image/gif") {
    return ".gif";
  }
  if (mime_type == "image/webp") {
    return ".webp";
  }
  if (mime_type == "video/mp4") {
    return ".mp4";
  }
  if (mime_type == "video/webm") {
    return ".webm";
  }
  if (mime_type == "video/quicktime") {
    return ".mov";
  }
  if (mime_type == "video/x-matroska") {
    return ".mkv";
  }
  if (mime_type == "audio/mpeg") {
    return ".mp3";
  }
  if (mime_type == "audio/ogg") {
    return ".ogg";
  }
  if (mime_type == "audio/wav") {
    return ".wav";
  }
  if (mime_type == "application/pdf") {
    return ".pdf";
  }
  if (mime_type == "text/plain") {
    return ".txt";
  }
  return ".dat";
}
}  // namespace

struct AttachmentRequest {
  std::string file_path;
  messaging::AttachmentType type;
  std::string thumbnail_base64;
  std::string mime_type;
};

class MessagingService {
 public:
  static constexpr uint32_t currentMessageVersion = 1;

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
        [this](ContactID const& peer_id) { return HasPendingWork(peer_id); }
    );
  }

  void StartDeliveryService() {
    // Restore transfers logic is implicitly handled by FileTransferService via queue checks if needed,
    // or by explicit resume requests.
    delivery_service_->Start();
  }

  void StopDeliveryService() { delivery_service_->Stop(); }

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

    RequestConnection(peer_id);

    std::vector<messaging::Attachment> domain_attachments;
    for (auto const& req : attachments) {
      std::uint64_t file_size = 0;
      std::string mime = req.mime_type;
      std::string hash;

      try {
        file_size = std::filesystem::file_size(req.file_path);
        hash = FileTransferService::CalculateFileHash(req.file_path);
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

      domain_attachments.push_back(
          {.id = GenerateAttachmentId(),
           .type = req.type,
           .file_path = req.file_path,
           .mime_type = mime,
           .size_bytes = file_size,
           .thumbnail_base64 = req.thumbnail_base64,
           .hash = hash,
           .is_downloaded = true,
           .status = messaging::AttachmentStatus::pending}
      );
    }

    auto entry = history_->AddTextEntry(
        peer_id,
        message,
        domain_attachments,
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::textMessage
    );

    if (auto peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }

    return entry;
  }

  void SendSystemMessage(ContactID const& peer_id, std::string const& content) {
    history_->AddTextEntry(
        peer_id,
        content,
        {},
        messaging::MessageDirection::sent,
        messaging::HistoryEntryKind::systemMessage
    );

    if (auto peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
  }

  // Attempts to flush queued messages and read receipts for a connected peer
  void SendQueuedMessages(ContactID const& peer_id) {
    // Flush generic pending actions (like read receipts)
    auto actions = storage_->GetPendingActions(peer_id);
    for (auto const& action : actions) {
      try {
        transport_->SendPeerDataChannelMessage(peer_id, nlohmann::json::parse(action.payload));
        storage_->RemovePendingAction(action.id);
      } catch (...) {
        storage_->RemovePendingAction(action.id);
      }
    }

    // Notify FileTransferService connection is ready
    file_transfer_service_->OnConnectionReady(peer_id);

    auto const messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
    auto const peer = transport_->GetPeer(peer_id);

    LGI << "Processing " << messages.size() << " queued message(s) for " << peer_id.ToString();

    bool any_message_sent = false;

    for (auto const& entry : messages) {
      bool sent = false;

      if (entry.kind != messaging::HistoryEntryKind::textMessage &&
          entry.kind != messaging::HistoryEntryKind::systemMessage) {
        continue;
      }

      if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        continue;
      }

      std::string content_to_send;
      auto const& msg_entry = std::get<messaging::MessageEntry>(entry.data);

      if (entry.is_truncated) {
        if (auto full = history_->GetFullMessageContent(peer_id, entry.id)) {
          content_to_send = *full;
        } else {
          continue;
        }
      } else {
        content_to_send = msg_entry.content;
      }

      if (entry.kind == messaging::HistoryEntryKind::systemMessage) {
        try {
          nlohmann::json content_json = nlohmann::json::parse(content_to_send);
          std::string const action = content_json.value("action", "");

          if (action == "contact_request" || action == "contact_confirm") {
            nlohmann::json msg = {
                {"type", action == "contact_request" ? "contact_add_request" : "contact_confirm"},
                {"to", peer_id.ToString()}
            };
            if (action == "contact_request") {
              if (content_json.contains("profile")) {
                msg["profile"] = content_json["profile"];
              }
              if (content_json.contains("token")) {
                msg["token"] = content_json["token"];
              }
            }
            transport_->SendSignalingMessage(msg);
            sent = true;
          }
        } catch (...) {
        }
      }

      if (!sent && peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
        std::string payload_with_version;
        payload_with_version.reserve(sizeof(currentMessageVersion) + content_to_send.size());
        payload_with_version.push_back(static_cast<char>(currentMessageVersion));
        payload_with_version.append(content_to_send);

        auto encrypted = peer->session_cipher->Encrypt(payload_with_version);
        UpdatePeerActivity(peer_id);

        if (!encrypted.empty()) {
          nlohmann::json json_msg = {
              {"type", "chat"},
              {"message_id", entry.id},
              {"is_system", (entry.kind == messaging::HistoryEntryKind::systemMessage)},
              {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
          };

          if (!msg_entry.attachments.empty()) {
            std::vector<nlohmann::json> att_list;
            att_list.reserve(msg_entry.attachments.size());
            for (auto const& att : msg_entry.attachments) {
              att_list.push_back(
                  {{"id", att.id},
                   {"type", att.type},
                   {"mime", att.mime_type},
                   {"size", att.size_bytes},
                   {"thumb", att.thumbnail_base64},
                   {"hash", att.hash}}
              );
            }
            json_msg["attachments"] = att_list;
          }

          transport_->SendPeerDataChannelMessage(peer_id, json_msg);
          sent = true;

          // Delegate upload
          if (!msg_entry.attachments.empty()) {
            for (auto const& att : msg_entry.attachments) {
              file_transfer_service_->QueueUpload(peer_id, att);
            }
          }
        }
      }

      if (sent) {
        history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
        any_message_sent = true;
      }
    }

    if (messages.empty() && !any_message_sent) {
      // NOTE: We don't check for active uploads here because ConnectionAttemptFinished is about
      // whether we need to keep retrying the *connection*. If uploads are active, the connection is alive.
      // If we failed to send messages, retry. If we sent all, done.
      delivery_service_->ConnectionAttemptFinished(peer_id);
    }
  }

  // Signal that a connection attempt finished (success or fail) to manage retry logic
  void OnConnectionAttemptFinished(ContactID const& peer_id) const {
    delivery_service_->ConnectionAttemptFinished(peer_id);
  }

  // File Transfer Control Delegates
  void CancelTransfer(
      ContactID const& peer_id,
      std::string const& attachment_id,
      bool is_upload,
      bool notify_peer = true
  ) const {
    file_transfer_service_->CancelTransfer(peer_id, attachment_id, is_upload, notify_peer);
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) const {
    file_transfer_service_->ResumeTransfer(peer_id, attachment_id, is_upload);
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) const {
    file_transfer_service_->DiscardAttachment(peer_id, attachment_id);
  }

  // Queries
  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
      -> messaging::HistoryPage {
    auto page = history_->GetHistoryPage(peer_id, page_index, page_size);

    bool any_modified = false;
    for (auto& entry : page.messages) {
      if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto& msg = std::get<messaging::MessageEntry>(entry.data);
        bool entry_modified = false;
        for (auto& att : msg.attachments) {
          if (!att.is_downloaded && !att.file_path.empty()) {
            std::error_code err;
            if (std::filesystem::exists(att.file_path, err) && std::filesystem::is_regular_file(att.file_path, err) &&
                std::filesystem::file_size(att.file_path, err) == att.size_bytes) {
              att.is_downloaded = true;
              att.status = messaging::AttachmentStatus::done;
              entry_modified = true;
            }
          }
        }
        if (entry_modified) {
          const_cast<MessagingService*>(this)->history_->UpdateHistoryEntry(entry);
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

  // Updates
  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids) {
    history_->MarkMessagesAsRead(peer_id, message_ids);

    auto remote_ids = storage_->GetRemoteIds(peer_id, message_ids);

    if (remote_ids.empty()) {
      return;
    }

    nlohmann::json const msg = {
        {"type", "message_read"},
        {"message_ids", remote_ids},
    };
    storage_->AddPendingAction(peer_id, msg.dump());

    if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      SendQueuedMessages(peer_id);
    } else {
      delivery_service_->AttemptDelivery(peer_id);
    }
  }

  // History Ingestion (from other services)
  void AddSystemEntry(ContactID const& peer_id, std::string const& content, messaging::MessageDirection direction) {
    history_->AddTextEntry(peer_id, content, {}, direction, messaging::HistoryEntryKind::systemMessage);
  }

  void AddCallEntry(
      ContactID const& peer_id,
      messaging::HistoryEntryKind kind,
      messaging::CallDirection direction,
      messaging::CallStatus status,
      uint32_t duration
  ) {
    history_->AddCallEntry(peer_id, kind, direction, status, duration);
  }

  // Incoming Events
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

    auto decrypted = peer->session_cipher->Decrypt({encrypted_payload.begin(), encrypted_payload.end()});
    if (decrypted.empty()) {
      LGE << "Failed to decrypt chat message from " << peer_id.ToString();
      return;
    }

    if (auto const received_version = static_cast<decltype(currentMessageVersion)>(decrypted[0]);
        received_version > currentMessageVersion) {
      std::string const error_msg = "Unsupported message of version " + std::to_string(received_version) +
                                    " received, maximum supported version is " + std::to_string(currentMessageVersion) +
                                    ", please update";

      auto entry = history_->AddTextEntry(
          peer_id,
          error_msg,
          {},
          messaging::MessageDirection::received,
          messaging::HistoryEntryKind::systemMessage,
          message_id
      );

      nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
      transport_->SendPeerDataChannelMessage(peer_id, receipt);

      INVARIANT(on_message_received_);
      on_message_received_(peer_id, entry);
      return;
    }

    std::string const content = decrypted.substr(1);

    // Make a mutable copy of attachments to update paths
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
        std::string const ext = GetExtensionForMimeType(att.mime_type);
        std::filesystem::path const final_path = attachments_dir / (att.id + ext);
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
        message_id
    );

    // Now start downloads
    if (!local_attachments.empty()) {
      for (auto const& att : local_attachments) {
        file_transfer_service_->QueueDownload(peer_id, att, entry.id);
      }
    }

    nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
    transport_->SendPeerDataChannelMessage(peer_id, receipt);

    INVARIANT(on_message_received_);
    on_message_received_(peer_id, entry);
  }

  void OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status) const {
    auto message_status = messaging::MessageStatus::sent;
    if (status == "delivered") {
      message_status = messaging::MessageStatus::delivered;
    } else if (status == "read") {
      message_status = messaging::MessageStatus::read;
    }

    history_->UpdateMessageStatus(peer_id, message_id, message_status);
  }

  void OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) const {
    for (auto id : message_ids) {
      history_->UpdateMessageStatus(peer_id, id, messaging::MessageStatus::read);
    }
  }

  // Callbacks
  using OnChatMessageCallback = std::function<void(ContactID const& peer_id, messaging::HistoryEntryRecord const&)>;
  using OnMessageStatusChangedCallback = std::function<void(ContactID const&, uint64_t, messaging::MessageStatus)>;
  using OnAttachmentDownloadedCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id)>;
  using OnContactRestoredCallback = std::function<void(ContactID const&)>;
  using OnTransferProgressCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id, float progress)>;
  using OnAttachmentStatusChangedCallback = std::function<
      void(ContactID const& peer_id, std::string const& attachment_id, messaging::AttachmentStatus status)>;

  void SetChatMessageReceivedCallback(OnChatMessageCallback cb) { on_message_received_ = std::move(cb); }
  void SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb) { on_status_changed_ = std::move(cb); }
  void SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb) {
    file_transfer_service_->SetAttachmentDownloadedCallback(std::move(cb));
  }
  void SetContactRestoredCallback(OnContactRestoredCallback cb) { on_contact_restored_ = std::move(cb); }
  void SetTransferProgressCallback(OnTransferProgressCallback cb) {
    file_transfer_service_->SetTransferProgressCallback(std::move(cb));
  }
  void SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb) {
    file_transfer_service_->SetAttachmentStatusChangedCallback(std::move(cb));
  }

 private:
  // Helper to connect to a peer if not connected
  void RequestConnection(ContactID const& peer_id) {
    if (auto const peer = transport_->GetPeer(peer_id);
        peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      SendQueuedMessages(peer_id);
    } else {
      connection_handler_(peer_id);
    }
  }

  void UpdatePeerActivity(ContactID const& peer_id) {
    if (auto peer = transport_->GetPeer(peer_id)) {
      peer->last_activity_time = Time::Now();
    }
  }

  // Checks DB for messages, read receipts, and contact profile sync flag
  auto HasPendingWork(ContactID const& peer_id) const -> bool {
    // Check pending actions (read receipts, profiles, etc.)
    if (auto const pending_actions = storage_->GetPendingActions(peer_id); !pending_actions.empty()) {
      return true;
    }

    // Check for active uploads/downloads that need network
    // Ideally check DB for 'transferring' or 'pending' attachments
    auto pending_atts = storage_->GetPendingAttachments(peer_id);
    if (!pending_atts.empty()) return true;

    return !storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending).empty();
  }

  // Generate a unique ID for attachments
  static auto GenerateAttachmentId() -> std::string {
    static std::random_device random_device;
    static std::mt19937 gen(random_device());
    static std::uniform_int_distribution<uint64_t> dis;
    std::stringstream sstream;
    sstream << std::hex << dis(gen) << dis(gen);
    return sstream.str();
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
};

}  // namespace eillance::services
