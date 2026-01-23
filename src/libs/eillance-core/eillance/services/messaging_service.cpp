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

void MessagingService::StartDeliveryService() { delivery_service_->Start(); }

void MessagingService::StopDeliveryService() { delivery_service_->Stop(); }

void MessagingService::RequestConnection(ContactID const& peer_id) {
  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
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
    std::string mime = "application/octet-stream";
    std::string hash;

    try {
      file_size = std::filesystem::file_size(req.file_path);
      hash = CalculateFileHash(req.file_path);
    } catch (...) {
      LGW << "Failed to process attachment file: " << req.file_path;
      continue;
    }

    if (req.type == messaging::AttachmentType::image) {
      mime = "image/jpeg";
    } else if (req.type == messaging::AttachmentType::video) {
      mime = "video/mp4";
    }

    domain_attachments.push_back(
        {.id = GenerateAttachmentId(),
         .type = req.type,
         .file_path = req.file_path,
         .mime_type = mime,
         .size_bytes = file_size,
         .thumbnail_base64 = req.thumbnail_base64,
         .hash = hash,
         .is_downloaded = true}
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
  SendPendingReadReceipts(peer_id);

  auto const messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  if (messages.empty()) {
    delivery_service_->ConnectionAttemptFinished(peer_id);
    return;
  }

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

  delivery_service_->ConnectionAttemptFinished(peer_id);
}

void MessagingService::StartUpload(ContactID const& peer_id, messaging::Attachment const& att) {
  {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_uploads_.contains(att.id)) {
      return;
    }

    std::ifstream file(att.file_path, std::ios::binary);
    if (!file) {
      LGE << "Failed to open file for upload: " << att.file_path;
      return;
    }

    TransferUploadState state;
    state.file_path = att.file_path;
    state.stream = std::move(file);
    state.sequence = 0;
    state.window_end = windowSizeChunks;
    state.total_size = att.size_bytes;
    state.hash = att.hash;

    active_uploads_[att.id] = std::move(state);
  }
  PumpUpload(peer_id, att.id);
}

void MessagingService::PumpUpload(ContactID const& peer_id, std::string const& file_id) {
  std::unique_lock<std::mutex> lock(transfers_mutex_);
  auto const itr = active_uploads_.find(file_id);
  if (itr == active_uploads_.end()) {
    return;
  }

  TransferUploadState& state = itr->second;

  while (state.sequence < state.window_end) {
    auto const offset = state.sequence * chunkSizeBytes;
    if (offset >= state.total_size && state.total_size > 0) {
      break;
    }

    state.stream.seekg(static_cast<std::streamoff>(offset));
    if (state.stream.fail()) {
      LGE << "Seek failed for upload " << file_id << " at offset " << offset;
      state.stream.clear();
      break;
    }

    std::vector<char> buffer(chunkSizeBytes);
    state.stream.read(buffer.data(), chunkSizeBytes);
    std::streamsize bytes_read = state.stream.gcount();

    if (bytes_read <= 0) {
      if (state.stream.eof()) {
        break;
      }
      break;
    }

    std::vector<std::byte> chunk_data(bytes_read);
    std::memcpy(chunk_data.data(), buffer.data(), bytes_read);

    std::string b64_data = crypto::ToHex(chunk_data);

    nlohmann::json chunk_msg =
        {{"type", "file_chunk"}, {"id", file_id}, {"seq", state.sequence}, {"data", b64_data}, {"eof", false}};

    transport_->SendPeerDataChannelMessage(peer_id, chunk_msg);
    state.sequence++;
  }

  bool is_stream_done = (state.sequence * chunkSizeBytes) >= state.total_size;

  if (is_stream_done && state.sequence < state.window_end) {
    nlohmann::json eof_msg =
        {{"type", "file_chunk"}, {"id", file_id}, {"seq", state.sequence}, {"data", ""}, {"eof", true}};
    transport_->SendPeerDataChannelMessage(peer_id, eof_msg);

    active_uploads_.erase(itr);
  }
}

void MessagingService::ProcessFileAck(ContactID const& peer_id, std::string const& file_id, size_t next_seq) {
  bool should_pump = false;
  {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (auto const itr = active_uploads_.find(file_id); itr != active_uploads_.end()) {
      itr->second.window_end = next_seq + windowSizeChunks;
      should_pump = true;
    }
  }
  if (should_pump) {
    PumpUpload(peer_id, file_id);
  }
}

void MessagingService::SendPendingReadReceipts(ContactID const& peer_id) {
  std::vector<uint64_t> ids_to_send;
  {
    std::lock_guard<std::recursive_mutex> const lock(receipts_mutex_);
    if (auto it = pending_read_receipts_.find(peer_id); it != pending_read_receipts_.end()) {
      ids_to_send = std::move(it->second);
      pending_read_receipts_.erase(it);
    }
  }

  if (ids_to_send.empty()) {
    return;
  }

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
    nlohmann::json const msg = {
        {"type", "message_read"},
        {"message_ids", ids_to_send},
    };
    transport_->SendPeerDataChannelMessage(peer_id, msg);
    LGI << "Sent pending read receipts for " << ids_to_send.size() << " messages to " << peer_id.ToString();
  } else {
    std::lock_guard<std::recursive_mutex> const lock(receipts_mutex_);
    auto& pending = pending_read_receipts_[peer_id];
    pending.insert(pending.end(), ids_to_send.begin(), ids_to_send.end());
  }
}

void MessagingService::OnConnectionAttemptFinished(ContactID const& peer_id) const {
  delivery_service_->ConnectionAttemptFinished(peer_id);
}

auto MessagingService::GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
    -> messaging::HistoryPage {
  return history_->GetHistoryPage(peer_id, page_index, page_size);
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

  {
    std::lock_guard<std::recursive_mutex> const lock(receipts_mutex_);
    auto& pending = pending_read_receipts_[peer_id];
    pending.insert(pending.end(), remote_ids.begin(), remote_ids.end());
  }

  auto peer = transport_->GetPeer(peer_id);
  if (peer && peer->data_channel_is_open) {
    SendPendingReadReceipts(peer_id);
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

  if (!attachments.empty()) {
    std::filesystem::path storage_root(storage_->GetPath());
    std::filesystem::path attachments_dir = storage_root.parent_path() / "attachments";

    std::error_code ec;
    std::filesystem::create_directories(attachments_dir, ec);
    if (ec) {
      LGE << "Failed to create attachments directory: " << attachments_dir.string() << " Error: " << ec.message();
    }

    std::unique_lock<std::mutex> lock(transfers_mutex_);
    for (auto const& att : attachments) {
      if (!att.is_downloaded) {
        if (active_downloads_.contains(att.id)) {
          continue;
        }

        TransferDownloadState state;
        state.file_path = attachments_dir / (att.id + ".dat");
        state.temp_path = attachments_dir / (att.id + ".tmp");
        state.expected_hash = att.hash;
        state.total_size = att.size_bytes;

        try {
          // Truncate/Create
          std::ofstream ofs(state.temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
          if (!ofs) {
            LGE << "Failed to create temp file: " << state.temp_path.string();
            continue;
          }
          ofs.close();
          // Pre-allocate
          std::filesystem::resize_file(state.temp_path, att.size_bytes);
        } catch (std::exception const& e) {
          LGE << "Failed to reserve space for attachment " << att.id << ": " << e.what();
          continue;
        }

        state.stream.open(state.temp_path,
                          std::ios::binary | std::ios::in | std::ios::out);  // NOLINT(*-mt-unsafe)

        if (!state.stream.is_open()) {
          LGE << "Failed to open output stream for attachment download: " << state.temp_path.string();
          continue;
        }

        LGI << "Initialized download for attachment " << att.id << " at " << state.temp_path.string();
        active_downloads_[att.id] = std::move(state);
      }
    }
  }

  auto entry = history_->AddTextEntry(
      peer_id,
      content,
      attachments,
      messaging::MessageDirection::received,
      is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage,
      message_id
  );

  nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
  transport_->SendPeerDataChannelMessage(peer_id, receipt);

  INVARIANT(on_message_received_);
  on_message_received_(peer_id, entry);
}

void MessagingService::ProcessIncomingFileChunk(ContactID const& peer_id, nlohmann::json const& message) {
  if (message.contains("ack_seq")) {
    std::string const file_id = message["id"];
    size_t const seq = message["ack_seq"];
    ProcessFileAck(peer_id, file_id, seq);
    return;
  }

  std::string const att_id = message["id"];
  size_t const seq = message["seq"];
  bool const eof = message["eof"];

  std::unique_lock<std::mutex> lock(transfers_mutex_);
  auto const itr = active_downloads_.find(att_id);
  if (itr == active_downloads_.end()) {
    LGE << "Received chunk for unknown or completed file transfer: " << att_id;
    return;
  }

  TransferDownloadState& state = itr->second;

  if (eof) {
    LGI << "EOF received for file " << att_id << ". Finalizing download.";
    auto const node = active_downloads_.extract(itr);
    lock.unlock();

    auto& extracted_state = node.mapped();
    extracted_state.stream.flush();
    extracted_state.stream.close();

    std::string calculated_hash = CalculateFileHash(extracted_state.temp_path);
    if (calculated_hash == extracted_state.expected_hash) {
      bool renamed = false;
      for (int i = 0; i < 20; ++i) {
        std::error_code ec;
        if (std::filesystem::exists(extracted_state.file_path, ec)) {
          std::filesystem::remove(extracted_state.file_path, ec);
        }
        std::filesystem::rename(extracted_state.temp_path, extracted_state.file_path, ec);
        if (!ec) {
          renamed = true;
          LGI << "File transfer complete and verified for " << att_id;
          break;
        }
        // If rename fails, try copy
        std::filesystem::copy_file(
            extracted_state.temp_path,
            extracted_state.file_path,
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
        if (!ec) {
          std::filesystem::remove(extracted_state.temp_path, ec);
          renamed = true;
          LGI << "File transfer complete (copy) for " << att_id;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      if (!renamed) {
        LGE << "Failed to move file for " << att_id << " after retries";
      }
    } else {
      LGE << "Hash mismatch for file " << att_id << ". Expected: " << extracted_state.expected_hash
          << ", Got: " << calculated_hash;
      std::error_code ec;
      std::filesystem::remove(extracted_state.temp_path, ec);
    }
  } else {
    std::string const hex_data = message["data"];
    auto bytes = crypto::FromHex(hex_data);

    if (state.stream.good()) {
      auto const offset = seq * chunkSizeBytes;
      state.stream.seekp(static_cast<std::streamoff>(offset));
      if (!state.stream.fail()) {
        state.stream.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
      } else {
        state.stream.clear();
        LGE << "Seek failed for download " << att_id << " at offset " << offset;
      }
    }

    if ((seq + 1) % windowSizeChunks == 0) {
      nlohmann::json ack_msg = {{"type", "file_chunk"}, {"id", att_id}, {"ack_seq", seq + 1}};
      lock.unlock();
      transport_->SendPeerDataChannelMessage(peer_id, ack_msg);
    }
  }
}

void MessagingService::OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status)
    const {
  auto s = messaging::MessageStatus::sent;
  if (status == "delivered") {
    s = messaging::MessageStatus::delivered;
  } else if (status == "read") {
    s = messaging::MessageStatus::read;
  }

  history_->UpdateMessageStatus(peer_id, message_id, s);
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

bool MessagingService::HasPendingWork(ContactID const& peer_id) {
  {
    std::lock_guard<std::recursive_mutex> const lock(receipts_mutex_);
    if (pending_read_receipts_.contains(peer_id) && !pending_read_receipts_[peer_id].empty()) {
      return true;
    }
  }
  return !storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending).empty();
}

auto MessagingService::GenerateAttachmentId() -> std::string {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint64_t> dis;
  std::stringstream ss;
  ss << std::hex << dis(gen) << dis(gen);
  return ss.str();
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
    if (!file) break;
  }
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_Final(hash, &sha256);
  file.close();

  std::vector<std::byte> hash_bytes(SHA256_DIGEST_LENGTH);
  std::memcpy(hash_bytes.data(), hash, SHA256_DIGEST_LENGTH);

  return crypto::ToHex(hash_bytes);
}

}  // namespace eillance::services
