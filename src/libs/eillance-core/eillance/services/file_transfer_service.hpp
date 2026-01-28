#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

class FileTransferService {
 public:
  static constexpr size_t windowSizeChunks = 64;
  static constexpr size_t chunkSizeBytes = 16ULL * 1024;

  explicit FileTransferService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)) {}

  // --- Public API ---

  // Called when a connection to a peer is established/ready to check for uploads
  void OnConnectionReady(ContactID const& peer_id) {
    auto pending = storage_->GetPendingAttachments(peer_id);
    for (auto const& att : pending) {
      if (att.is_downloaded) {
        // Upload
        {
          std::lock_guard<std::mutex> const lock(transfers_mutex_);
          if (!active_uploads_.contains(att.id)) {
            StartUploadInternal(peer_id, att);
          } else {
            // Already active, ensure not paused
            auto& state = active_uploads_[att.id];
            state.paused = false;
            // Send resume request/handshake
            nlohmann::json const resume_req = {{"type", "file_resume_request"}, {"id", att.id}};
            transport_->SendPeerDataChannelMessage(peer_id, resume_req);
          }
        }
      } else {
        // Download - request resume
        ResumeTransfer(peer_id, att.id, false);
      }
    }
  }

  void QueueUpload(ContactID const& peer_id, messaging::Attachment const& att) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    StartUploadInternal(peer_id, att);
  }

  void QueueDownload(ContactID const& peer_id, messaging::Attachment const& att, uint64_t message_id) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (active_downloads_.contains(att.id)) {
      return;
    }

    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    std::filesystem::path const temp_path = attachments_dir / (att.id + ".tmp");

    TransferDownloadState state;
    state.file_path = std::filesystem::path(att.file_path);
    state.temp_path = temp_path;
    state.expected_hash = att.hash;
    state.total_size = att.size_bytes;
    state.message_id = message_id;

    try {
      // Truncate/Create
      std::ofstream ofs(state.temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
      if (!ofs) {
        LGE << "Failed to create temp file: " << state.temp_path.string();
        return;
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
      return;
    }

    state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!state.stream.is_open()) {
      LGE << "Failed to open output stream for attachment download: " << state.temp_path.string();
      return;
    }

    active_downloads_[att.id] = std::move(state);
  }

  void ProcessMessage(ContactID const& peer_id, nlohmann::json const& message) {
    std::string const type = message["type"];
    std::string const file_id = message["id"];

    if (type == "file_cancel") {
      ProcessFileCancel(peer_id, file_id);
      return;
    }

    if (type == "file_resume_request") {
      auto att = storage_->GetAttachment(file_id);
      if (att && att->is_downloaded) {
        // Upload request from peer
        std::lock_guard<std::mutex> const lock(transfers_mutex_);
        if (!active_uploads_.contains(file_id)) {
          StartUploadInternal(peer_id, *att);
        }
        return;
      }

      // Download request (peer has it, we want it?)
      // Actually "file_resume_request" is usually sent by the *receiver* to the *sender* to ask for data.
      // If we receive it, we are the sender.
      return;
    }

    if (message.contains("ack_seq")) {
      size_t const seq = message["ack_seq"];
      ProcessFileAck(peer_id, file_id, seq);
      return;
    }

    if (type == "file_chunk") {
      size_t const seq = message["seq"];
      bool const eof = message["eof"];
      std::string const data_hex = message.value("data", "");
      ProcessIncomingChunk(peer_id, file_id, seq, data_hex, eof);
    }
  }

  void
  CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload, bool notify_peer = true) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);

    if (is_upload) {
      active_uploads_.erase(attachment_id);
      if (notify_peer) {
        nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"id", attachment_id}};
        transport_->SendPeerDataChannelMessage(peer_id, cancel_msg);
      }
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, true);
    } else {
      if (active_downloads_.contains(attachment_id)) {
        auto& state = active_downloads_[attachment_id];
        state.stream.close();
        std::error_code ec;
        std::filesystem::remove(state.temp_path, ec);
        active_downloads_.erase(attachment_id);
      }
      if (notify_peer) {
        nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"id", attachment_id}};
        transport_->SendPeerDataChannelMessage(peer_id, cancel_msg);
      }
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, false);
    }

    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::canceled);
    }
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) {
    if (is_upload) {
      auto att = storage_->GetAttachment(attachment_id);
      if (att) {
        std::lock_guard<std::mutex> const lock(transfers_mutex_);
        StartUploadInternal(peer_id, *att);
      }
    } else {
      nlohmann::json const pull_msg = {{"type", "file_resume_request"}, {"id", attachment_id}};
      transport_->SendPeerDataChannelMessage(peer_id, pull_msg);
    }
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) {
    CancelTransfer(peer_id, attachment_id, true, true);
    CancelTransfer(peer_id, attachment_id, false, true);

    storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::discarded, false);
    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::discarded);
    }
  }

  static auto CalculateFileHash(std::filesystem::path const& path) -> std::string {
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
      std::streamsize const bytes_read = file.gcount();
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

  // --- Callbacks ---
  using OnAttachmentDownloadedCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id)>;
  using OnTransferProgressCallback =
      std::function<void(ContactID const& peer_id, std::string const& attachment_id, float progress)>;
  using OnAttachmentStatusChangedCallback = std::function<
      void(ContactID const& peer_id, std::string const& attachment_id, messaging::AttachmentStatus status)>;

  void SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb) { on_attachment_downloaded_ = std::move(cb); }
  void SetTransferProgressCallback(OnTransferProgressCallback cb) { on_transfer_progress_ = std::move(cb); }
  void SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb) {
    on_attachment_status_changed_ = std::move(cb);
  }

 private:
  struct TransferUploadState {
    std::string file_path;
    std::ifstream stream;
    size_t sequence = 0;
    size_t window_end = 0;
    size_t total_size = 0;
    std::string hash;
    bool paused = false;
  };

  struct TransferDownloadState {
    std::filesystem::path file_path;
    std::fstream stream;
    uint64_t total_size = 0;
    std::string expected_hash;
    std::filesystem::path temp_path;
    uint64_t message_id = 0;
    size_t highest_seq_written = 0;
  };

  void StartUploadInternal(ContactID const& peer_id, messaging::Attachment const& att) {
    // Expects lock to be held or called from context where appropriate
    if (active_uploads_.contains(att.id)) {
      auto& state = active_uploads_[att.id];
      state.paused = false;
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

    storage_->UpdateAttachmentStatus(att.id, messaging::AttachmentStatus::transferring, true);
    if (on_attachment_status_changed_)
      on_attachment_status_changed_(peer_id, att.id, messaging::AttachmentStatus::transferring);

    // Send Resume Request to sync
    nlohmann::json const resume_req = {{"type", "file_resume_request"}, {"id", att.id}};
    transport_->SendPeerDataChannelMessage(peer_id, resume_req);
  }

  void PumpUpload(ContactID const& peer_id, std::string const& file_id) {
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
        if (state.stream.eof()) break;
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
        if (progress > 1.0F) progress = 1.0F;
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

      if (on_transfer_progress_) on_transfer_progress_(peer_id, file_id, 1.0f);
      if (on_attachment_status_changed_)
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
    }
  }

  void ProcessFileAck(ContactID const& peer_id, std::string const& file_id, size_t next_seq) {
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

  void ProcessFileCancel(ContactID const& peer_id, std::string const& file_id) {
    bool is_upload = false;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      is_upload = active_uploads_.contains(file_id);
    }
    CancelTransfer(peer_id, file_id, is_upload, false);
  }

  void ProcessIncomingChunk(
      ContactID const& peer_id,
      std::string const& file_id,
      size_t seq,
      std::string const& data_hex,
      bool eof
  ) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);

    // If download not active, try to initialize it (Resilience / Resume logic)
    if (!active_downloads_.contains(file_id)) {
      auto att = storage_->GetAttachment(file_id);
      if (!att) return;

      if (att->status == messaging::AttachmentStatus::canceled ||
          att->status == messaging::AttachmentStatus::discarded) {
        // Send cancel to stop sender
        nlohmann::json const cancel_msg = {{"type", "file_cancel"}, {"id", file_id}};
        if (lock.owns_lock()) lock.unlock();
        transport_->SendPeerDataChannelMessage(peer_id, cancel_msg);
        return;
      }

      // Re-initialize logic similar to QueueDownload but assuming file structure exists
      std::filesystem::path const storage_root(storage_->GetPath());
      std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
      std::filesystem::path const temp_path = attachments_dir / (att->id + ".tmp");

      TransferDownloadState state;
      state.file_path = std::filesystem::path(att->file_path);
      state.temp_path = temp_path;
      state.expected_hash = att->hash;
      state.total_size = att->size_bytes;
      state.message_id = 0;  // We might not have this easily, but it's okay

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
      LGI << "EOF received for file " << file_id;
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

        if (on_attachment_downloaded_) on_attachment_downloaded_(peer_id, file_id);
        if (on_transfer_progress_) on_transfer_progress_(peer_id, file_id, 1.0F);
        if (on_attachment_status_changed_)
          on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
      } else {
        LGE << "Hash mismatch for file " << file_id;
        storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::failed, false);
        if (on_attachment_status_changed_)
          on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::failed);
        std::filesystem::remove(extracted_state.temp_path);
      }
    } else {
      auto bytes = crypto::FromHex(data_hex);

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

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::map<std::string, TransferUploadState> active_uploads_;
  std::map<std::string, TransferDownloadState> active_downloads_;
  mutable std::mutex transfers_mutex_;

  OnAttachmentDownloadedCallback on_attachment_downloaded_;
  OnTransferProgressCallback on_transfer_progress_;
  OnAttachmentStatusChangedCallback on_attachment_status_changed_;
};

}  // namespace eillance::services
