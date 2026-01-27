#include "eillance/services/messaging_service.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>

#include <openssl/sha.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

namespace {
auto GetExtensionForMimeType(std::string_view const mime_type) -> std::string {
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

MessagingService::MessagingService(
    std::shared_ptr<storage::PersistentStorage> storage,
    std::shared_ptr<transport::Transport> transport,
    std::shared_ptr<identity::ContactManager> contact_manager,
    ConnectionRequestHandler connection_handler
)
    : storage_(std::move(storage)),
      transport_(std::move(transport)),
      contact_manager_(std::move(contact_manager)),
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

void MessagingService::StartDeliveryService() {
  RestoreTransfers();
  delivery_service_->Start();
}

void MessagingService::StopDeliveryService() { delivery_service_->Stop(); }

void MessagingService::RestoreTransfers() {
  // Recover downloads
  auto const contacts = contact_manager_->GetContacts();
  for (auto const& contact : contacts) {
    auto pending = storage_->GetPendingAttachments(contact.peer_id);
    for (auto& att : pending) {
      // Re-hydrate state logic if needed, or rely on resume requests.
      // Currently, we just ensure internal structures are clean.
      // Active resumption happens when user or peer triggers it.
    }
  }
}

void MessagingService::RequestConnection(ContactID const& peer_id) {
  if (auto const peer = transport_->GetPeer(peer_id);
      peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    connection_handler_(peer_id);
  }
}

void MessagingService::UpdatePeerActivity(ContactID const& peer_id) {
  if (auto peer = transport_->GetPeer(peer_id)) {
    peer->last_activity_time = Time::Now();
  }
}

auto MessagingService::SendChatMessage(
    ContactID const& peer_id,
    std::string const& message,
    std::vector<AttachmentRequest> const& attachments
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
      hash = CalculateFileHash(req.file_path);
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

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }

  return entry;
}

void MessagingService::SendSystemMessage(ContactID const& peer_id, std::string const& content) {
  history_->AddTextEntry(
      peer_id,
      content,
      {},
      messaging::MessageDirection::sent,
      messaging::HistoryEntryKind::systemMessage
  );

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }
}

void MessagingService::SendQueuedMessages(ContactID const& peer_id) {
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

  CheckForResumableUploads(peer_id);

  auto const messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  auto const peer = transport_->GetPeer(peer_id);

  LGI << "Processing " << messages.size() << " queued message(s) for " << peer_id.ToString();

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

        if (!msg_entry.attachments.empty()) {
          for (auto const& att : msg_entry.attachments) {
            StartUpload(peer_id, att);
          }
        }
      }
    }

    if (sent) {
      history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
    }
  }

  if (messages.empty() && active_uploads_.empty()) {
    delivery_service_->ConnectionAttemptFinished(peer_id);
  }
}

void MessagingService::CheckForResumableUploads(ContactID const& peer_id) {
  auto pending = storage_->GetPendingAttachments(peer_id);
  for (auto const& att : pending) {
    if (att.is_downloaded) {
      if (!active_uploads_.contains(att.id)) {
        StartUpload(peer_id, att);
      } else {
        // Even if active, ensure we try to resume/handshake
        StartUpload(peer_id, att);
      }
    }
  }
}

void MessagingService::StartUpload(ContactID const& peer_id, messaging::Attachment const& att) {
  {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_uploads_.contains(att.id)) {
      auto& state = active_uploads_[att.id];
      if (state.paused) {
        state.paused = false;
      }
      // Send resume request below (outside lock to avoid recursion issues if transport calls back)
    } else {
      std::ifstream file(att.file_path, std::ios::binary);
      if (!file) {
        LGE << "Failed to open file for upload: " << att.file_path;
        storage_->UpdateAttachmentStatus(att.id, messaging::AttachmentStatus::failed, true);
        if (on_attachment_status_changed_)
          on_attachment_status_changed_(peer_id, att.id, messaging::AttachmentStatus::failed);
        return;
      }

      TransferUploadState state;
      state.file_path = att.file_path;
      state.stream = std::move(file);
      state.sequence = 0;
      state.window_end = windowSizeChunks;  // Initial window
      state.total_size = att.size_bytes;
      state.hash = att.hash;

      active_uploads_[att.id] = std::move(state);
    }
  }

  storage_->UpdateAttachmentStatus(att.id, messaging::AttachmentStatus::transferring, true);
  if (on_attachment_status_changed_)
    on_attachment_status_changed_(peer_id, att.id, messaging::AttachmentStatus::transferring);

  // Always send Resume Request to synchronize state when starting/resuming
  nlohmann::json const resume_req = {{"type", "file_resume_request"}, {"id", att.id}};
  transport_->SendPeerDataChannelMessage(peer_id, resume_req);
}

void MessagingService::PumpUpload(ContactID const& peer_id, std::string const& file_id) {
  std::unique_lock<std::mutex> const lock(transfers_mutex_);
  auto const itr = active_uploads_.find(file_id);
  if (itr == active_uploads_.end() || itr->second.paused) {
    return;
  }

  TransferUploadState& state = itr->second;

  while (state.sequence < state.window_end) {
    auto const offset = state.sequence * chunkSizeBytes;
    if (offset >= state.total_size && state.total_size > 0) {
      break;
    }

    state.stream.clear();
    state.stream.seekg(static_cast<std::streamoff>(offset));
    if (state.stream.fail()) {
      LGE << "Seek failed for upload " << file_id << " at offset " << offset;
      state.stream.clear();
      break;
    }

    std::vector<char> buffer(chunkSizeBytes);
    state.stream.read(buffer.data(), chunkSizeBytes);
    auto const bytes_read = state.stream.gcount();

    if (bytes_read <= 0) {
      if (state.stream.eof()) {
        break;
      }
      break;
    }

    std::vector<std::byte> chunk_data(bytes_read);
    std::memcpy(chunk_data.data(), buffer.data(), bytes_read);

    std::string b64_data = crypto::ToHex(chunk_data);

    nlohmann::json const chunk_msg =
        {{"type", "file_chunk"}, {"id", file_id}, {"seq", state.sequence}, {"data", b64_data}, {"eof", false}};

    transport_->SendPeerDataChannelMessage(peer_id, chunk_msg);
    state.sequence++;

    if (on_transfer_progress_ && state.total_size > 0) {
      float progress = static_cast<float>(offset + bytes_read) / static_cast<float>(state.total_size);
      if (progress > 1.0F) {
        progress = 1.0F;
      }
      on_transfer_progress_(peer_id, file_id, progress);
    }
  }

  bool is_stream_done = (state.sequence * chunkSizeBytes) >= state.total_size;

  if (is_stream_done && state.sequence < state.window_end) {
    nlohmann::json const eof_msg =
        {{"type", "file_chunk"}, {"id", file_id}, {"seq", state.sequence}, {"data", ""}, {"eof", true}};
    transport_->SendPeerDataChannelMessage(peer_id, eof_msg);

    active_uploads_.erase(itr);
    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);

    if (on_transfer_progress_) {
      on_transfer_progress_(peer_id, file_id, 1.0f);
    }
    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
    }
  }
}

void MessagingService::ProcessFileAck(ContactID const& peer_id, std::string const& file_id, size_t next_seq) {
  bool should_pump = false;
  {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (auto const itr = active_uploads_.find(file_id); itr != active_uploads_.end()) {
      if (next_seq > itr->second.sequence) {
        itr->second.sequence = next_seq;
      }
      itr->second.window_end = next_seq + windowSizeChunks;
      should_pump = true;
    }
  }
  if (should_pump) {
    PumpUpload(peer_id, file_id);
  }
}

void MessagingService::ProcessFileCancel(ContactID const& peer_id, std::string const& file_id) {
  CancelTransfer(peer_id, file_id, false);
}

void MessagingService::ProcessResumeRequest(ContactID const& peer_id, std::string const& file_id) {
  size_t next_seq = 0;

  std::unique_lock<std::mutex> lock(transfers_mutex_);
  if (auto const itr = active_downloads_.find(file_id); itr != active_downloads_.end()) {
    next_seq = itr->second.highest_seq_written;
  } else {
    auto att = storage_->GetAttachment(file_id);
    if (att && !att->is_downloaded) {
      std::filesystem::path const storage_root(storage_->GetPath());
      std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
      std::filesystem::path const temp_path = attachments_dir / (att->id + ".tmp");

      if (std::filesystem::exists(temp_path)) {
        auto size = std::filesystem::file_size(temp_path);
        next_seq = size / chunkSizeBytes;
      }

      TransferDownloadState state;
      state.file_path = std::filesystem::path(att->file_path);
      state.temp_path = temp_path;
      state.expected_hash = att->hash;
      state.total_size = att->size_bytes;
      state.message_id = 0;

      state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
      if (!state.stream.is_open()) {
        state.stream.open(state.temp_path, std::ios::binary | std::ios::out);
        state.stream.close();
        state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
      }
      state.highest_seq_written = next_seq;

      active_downloads_[file_id] = std::move(state);

      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::transferring, false);
      if (on_attachment_status_changed_) {
        lock.unlock();
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::transferring);
        lock.lock();
      }
    }
  }

  lock.unlock();
  nlohmann::json const ack_msg = {{"type", "file_chunk"}, {"id", file_id}, {"ack_seq", next_seq}};
  transport_->SendPeerDataChannelMessage(peer_id, ack_msg);
}

void MessagingService::CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) {
  std::lock_guard<std::mutex> const lock(transfers_mutex_);

  if (is_upload) {
    if (active_uploads_.contains(attachment_id)) {
      active_uploads_.erase(attachment_id);
    }
    nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"id", attachment_id}};
    transport_->SendPeerDataChannelMessage(peer_id, cancel_msg);
    storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, true);
  } else {
    if (active_downloads_.contains(attachment_id)) {
      auto& state = active_downloads_[attachment_id];
      state.stream.close();
      std::error_code ec;
      std::filesystem::remove(state.temp_path, ec);
      active_downloads_.erase(attachment_id);
    }
    nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"id", attachment_id}};
    transport_->SendPeerDataChannelMessage(peer_id, cancel_msg);
    storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, false);
  }

  if (on_attachment_status_changed_) {
    on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::canceled);
  }
}

void MessagingService::ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) {
  if (is_upload) {
    auto att = storage_->GetAttachment(attachment_id);
    if (att) {
      StartUpload(peer_id, *att);
    }
  } else {
    nlohmann::json const pull_msg = {{"type", "file_resume_request"}, {"id", attachment_id}};
    transport_->SendPeerDataChannelMessage(peer_id, pull_msg);
  }
}

void MessagingService::OnConnectionAttemptFinished(ContactID const& peer_id) const {
  delivery_service_->ConnectionAttemptFinished(peer_id);
}

auto MessagingService::GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
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

auto MessagingService::GetFullMessageContent(ContactID const& peer_id, uint64_t message_id) const
    -> std::optional<std::string> {
  return history_->GetFullMessageContent(peer_id, message_id);
}

auto MessagingService::GetChatSummaries(std::vector<ContactID> const& peer_ids
) const -> std::vector<messaging::ChatSummary> {
  return history_->GetChatSummaries(peer_ids);
}

void MessagingService::MarkMessagesAsRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) {
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

void MessagingService::AddSystemEntry(
    ContactID const& peer_id,
    std::string const& content,
    messaging::MessageDirection direction
) {
  history_->AddTextEntry(peer_id, content, {}, direction, messaging::HistoryEntryKind::systemMessage);
}

void MessagingService::AddCallEntry(
    ContactID const& peer_id,
    messaging::HistoryEntryKind kind,
    messaging::CallDirection direction,
    messaging::CallStatus status,
    uint32_t duration
) {
  history_->AddCallEntry(peer_id, kind, direction, status, duration);
}

void MessagingService::OnChatMessageReceived(
    ContactID const& peer_id,
    std::vector<std::byte> const& encrypted_payload,
    uint64_t message_id,
    bool is_system,
    std::vector<messaging::Attachment> const& attachments
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
    std::unique_lock<std::mutex> const lock(transfers_mutex_);
    for (auto const& att : local_attachments) {
      if (!att.is_downloaded) {
        if (active_downloads_.contains(att.id)) {
          continue;
        }

        TransferDownloadState state;
        state.file_path = std::filesystem::path(att.file_path);
        state.temp_path = attachments_dir / (att.id + ".tmp");
        state.expected_hash = att.hash;
        state.total_size = att.size_bytes;
        state.message_id = entry.id;

        try {
          // Truncate/Create
          std::ofstream ofs(state.temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
          if (!ofs) {
            LGE << "Failed to create temp file: " << state.temp_path.string();
            continue;
          }
          ofs.close();
          // Pre-allocate
          try {
            std::filesystem::resize_file(state.temp_path, att.size_bytes);
          } catch (std::exception const& e) {
            LGW << "Failed to resize temp file for attachment " << att.id << ": " << e.what();
          }
        } catch (std::exception const& e) {
          LGE << "Failed to reserve space for attachment " << att.id << ": " << e.what();
          continue;
        }

        state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);

        if (!state.stream.is_open()) {
          LGE << "Failed to open output stream for attachment download: " << state.temp_path.string();
          continue;
        }

        active_downloads_[att.id] = std::move(state);
      }
    }
  }

  nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
  transport_->SendPeerDataChannelMessage(peer_id, receipt);

  INVARIANT(on_message_received_);
  on_message_received_(peer_id, entry);
}

void MessagingService::ProcessIncomingFileChunk(ContactID const& peer_id, nlohmann::json const& message) {
  std::string const type = message["type"];
  std::string const file_id = message["id"];

  if (type == "file_cancel") {
    ProcessFileCancel(peer_id, file_id);
    return;
  }

  if (type == "file_resume_request") {
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (active_uploads_.contains(file_id)) {
        return;
      }
    }

    ProcessResumeRequest(peer_id, file_id);
    return;
  }

  if (message.contains("ack_seq")) {
    size_t const seq = message["ack_seq"];
    ProcessFileAck(peer_id, file_id, seq);
    return;
  }

  size_t const seq = message["seq"];
  bool const eof = message["eof"];

  std::unique_lock<std::mutex> lock(transfers_mutex_);

  // If download not active, try to initialize it (Resilience)
  if (!active_downloads_.contains(file_id)) {
    auto att = storage_->GetAttachment(file_id);
    if (!att) return;

    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    std::filesystem::path const temp_path = attachments_dir / (att->id + ".tmp");

    TransferDownloadState state;
    state.file_path = std::filesystem::path(att->file_path);
    state.temp_path = temp_path;
    state.expected_hash = att->hash;
    state.total_size = att->size_bytes;
    state.message_id = 0;

    state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!state.stream.is_open()) {
      state.stream.open(state.temp_path, std::ios::binary | std::ios::out);
      state.stream.close();
      state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
    }

    active_downloads_[file_id] = std::move(state);

    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::transferring, false);
    if (on_attachment_status_changed_) {
      lock.unlock();
      on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::transferring);
      lock.lock();
    }
  }

  auto const itr = active_downloads_.find(file_id);
  if (itr == active_downloads_.end()) return;

  TransferDownloadState& state = itr->second;

  if (eof) {
    LGI << "EOF received for file " << file_id << " (seq " << seq << "). Finalizing download.";
    auto const node = active_downloads_.extract(itr);
    lock.unlock();

    auto& extracted_state = node.mapped();
    extracted_state.stream.flush();
    extracted_state.stream.close();

    std::string const calculated_hash = CalculateFileHash(extracted_state.temp_path);

    if (calculated_hash == extracted_state.expected_hash) {
      std::error_code err;
      std::filesystem::rename(extracted_state.temp_path, extracted_state.file_path, err);
      if (err) {
        std::filesystem::copy_file(
            extracted_state.temp_path,
            extracted_state.file_path,
            std::filesystem::copy_options::overwrite_existing,
            err
        );
        std::filesystem::remove(extracted_state.temp_path, err);
      }

      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);

      if (on_attachment_downloaded_) {
        on_attachment_downloaded_(peer_id, file_id);
      }
      if (on_transfer_progress_) {
        on_transfer_progress_(peer_id, file_id, 1.0F);
      }
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
      }
    } else {
      LGE << "Hash mismatch for file " << file_id;
      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::failed, false);
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::failed);
      }
      std::filesystem::remove(extracted_state.temp_path);
    }
  } else {
    std::string const hex_data = message["data"];
    auto bytes = crypto::FromHex(hex_data);

    if (state.stream.good()) {
      auto const offset = seq * chunkSizeBytes;
      state.stream.seekp(static_cast<std::streamoff>(offset));
      if (!state.stream.fail()) {
        state.stream.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());

        if (seq + 1 > state.highest_seq_written) {
          state.highest_seq_written = seq + 1;
        }
      } else {
        state.stream.clear();
      }
    }

    if (on_transfer_progress_ && state.total_size > 0) {
      auto const current_bytes = (seq * chunkSizeBytes) + bytes.size();
      float progress = static_cast<float>(current_bytes) / static_cast<float>(state.total_size);
      if (progress > 1.0F) progress = 1.0F;
      lock.unlock();
      on_transfer_progress_(peer_id, file_id, progress);
      lock.lock();
    }

    if ((seq + 1) % windowSizeChunks == 0) {
      nlohmann::json const ack_msg = {{"type", "file_chunk"}, {"id", file_id}, {"ack_seq", seq + 1}};
      if (lock.owns_lock()) lock.unlock();
      transport_->SendPeerDataChannelMessage(peer_id, ack_msg);
    }
  }
}

void MessagingService::OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status)
    const {
  auto message_status = messaging::MessageStatus::sent;
  if (status == "delivered") {
    message_status = messaging::MessageStatus::delivered;
  } else if (status == "read") {
    message_status = messaging::MessageStatus::read;
  }

  history_->UpdateMessageStatus(peer_id, message_id, message_status);
}

void MessagingService::OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) const {
  for (auto id : message_ids) {
    history_->UpdateMessageStatus(peer_id, id, messaging::MessageStatus::read);
  }
}

void MessagingService::SetChatMessageReceivedCallback(OnChatMessageCallback cb) {
  on_message_received_ = std::move(cb);
}
void MessagingService::SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb) {
  on_status_changed_ = std::move(cb);
}
void MessagingService::SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb) {
  on_attachment_downloaded_ = std::move(cb);
}
void MessagingService::SetContactRestoredCallback(OnContactRestoredCallback cb) {
  on_contact_restored_ = std::move(cb);
}
void MessagingService::SetTransferProgressCallback(OnTransferProgressCallback cb) {
  on_transfer_progress_ = std::move(cb);
}
void MessagingService::SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb) {
  on_attachment_status_changed_ = std::move(cb);
}

auto MessagingService::HasPendingWork(ContactID const& peer_id) const -> bool {
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

auto MessagingService::GenerateAttachmentId() -> std::string {
  static std::random_device random_device;
  static std::mt19937 gen(random_device());
  static std::uniform_int_distribution<uint64_t> dis;
  std::stringstream sstream;
  sstream << std::hex << dis(gen) << dis(gen);
  return sstream.str();
}

auto MessagingService::CalculateFileHash(std::filesystem::path const& path) -> std::string {
  std::ifstream file;
  bool opened = false;
  for (int i = 0; i < 20; ++i) {
    file.clear();
    file.open(path, std::ios::binary);
    if (file.is_open()) {
      opened = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  if (!opened) {
    LGE << "CalculateFileHash: Failed to open file " << path.string();
    return "";
  }

  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  char buffer[4096];
  while (true) {
    file.read(buffer, sizeof(buffer));
    std::streamsize bytes_read = file.gcount();
    if (bytes_read > 0) {
      SHA256_Update(&sha256, buffer, static_cast<size_t>(bytes_read));
    }
    if (!file) {
      break;
    }
  }
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_Final(hash, &sha256);
  file.close();

  std::vector<std::byte> hash_bytes(SHA256_DIGEST_LENGTH);
  std::memcpy(hash_bytes.data(), hash, SHA256_DIGEST_LENGTH);

  return crypto::ToHex(hash_bytes);
}

}  // namespace eillance::services
