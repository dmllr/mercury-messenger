#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class FileTransferService {
 public:
  static constexpr size_t kChunkSizeBytes = 16ULL * 1024;  // 16KB per chunk
  static constexpr size_t kWindowSizeChunks = 16;

  explicit FileTransferService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)) {}

  void OnConnectionReady(ContactID const& peer_id) {
    auto pending = storage_->GetPendingAttachments(peer_id);
    for (auto const& att : pending) {
      if (att.is_downloaded) {
        // We are uploader
        std::lock_guard<std::mutex> const lock(transfers_mutex_);
        if (!active_uploads_.contains(att.id)) {
          StartUploadInternal(peer_id, att);
        } else {
          // Send resume req / poke
          // This triggers the receiver to send back an ACK if they have partial data
          // SendBinary: Type Resume (0x05)
          SendResumeRequest(peer_id, att.id);
        }
      } else {
        // We are downloader
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
    if (active_downloads_.contains(att.id)) return;

    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    // Ensure dir exists
    std::filesystem::create_directories(attachments_dir);

    std::filesystem::path const temp_path = attachments_dir / (att.id + ".tmp");

    TransferDownloadState state;
    state.file_path = std::filesystem::path(att.file_path);
    state.temp_path = temp_path;
    state.expected_hash = att.hash;
    state.total_size = att.size_bytes;
    state.message_id = message_id;

    // Handle file creation and pre-allocation
    try {
      if (!std::filesystem::exists(state.temp_path)) {
        std::ofstream ofs(state.temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
        ofs.close();
        if (att.size_bytes > 0) {
          std::filesystem::resize_file(state.temp_path, att.size_bytes);
        }
      } else {
        // If it exists, ensure it is at least the expected size
        if (std::filesystem::file_size(state.temp_path) < att.size_bytes) {
          std::filesystem::resize_file(state.temp_path, att.size_bytes);
        }
      }
    } catch (std::exception const& e) {
      LGE << "Failed to reserve space for attachment " << att.id << ": " << e.what();
      // Continue anyway, it might work if we append, but we use random access so...
    }

    state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!state.stream.is_open()) {
      LGE << "Failed to open temp file for download: " << state.temp_path.string();
      return;
    }

    // Since we pre-allocate, we cannot blindly rely on file size for resume progress in this simple version.
    // We reset to 0 to be safe and overwrite.
    // In a production system, we'd use a bitmap or separate state file to track received chunks.
    state.highest_seq_written = 0;
    state.bytes_written = 0;

    active_downloads_[att.id] = std::move(state);
  }

  enum class MsgType : uint8_t { Chunk = 0x01, Ack = 0x02, Cancel = 0x03, Resume = 0x05, End = 0x06 };

  void ProcessBinaryMessage(ContactID const& peer_id, std::span<std::byte const> data) {
    if (data.size() < 4) return;
    if (data[0] != std::byte{0xFE}) return;

    MsgType type = static_cast<MsgType>(data[1]);
    uint8_t id_len = static_cast<uint8_t>(data[2]);
    if (data.size() < 3 + id_len) return;

    std::string file_id;
    file_id.resize(id_len);
    std::memcpy(file_id.data(), data.data() + 3, id_len);

    std::span<std::byte const> payload = data.subspan(3 + id_len);

    switch (type) {
      case MsgType::Chunk:
        HandleChunk(peer_id, file_id, payload);
        break;
      case MsgType::Ack:
        HandleAck(peer_id, file_id, payload);
        break;
      case MsgType::Cancel:
        HandleCancel(peer_id, file_id);
        break;
      case MsgType::Resume:
        HandleResume(peer_id, file_id);
        break;
      case MsgType::End:
        HandleEnd(peer_id, file_id);
        break;
    }
  }

  void
  CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload, bool notify_peer = true) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (is_upload) {
      active_uploads_.erase(attachment_id);
    } else {
      if (active_downloads_.contains(attachment_id)) {
        auto& state = active_downloads_[attachment_id];
        state.stream.close();
        // Remove temp file on cancel to cleanup
        std::error_code ec;
        std::filesystem::remove(state.temp_path, ec);
        active_downloads_.erase(attachment_id);
      }
    }

    if (notify_peer) {
      SendCancel(peer_id, attachment_id);
    }

    storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, is_upload);
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
      SendResumeRequest(peer_id, attachment_id);
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

  using OnAttachmentDownloadedCallback = std::function<void(ContactID const&, std::string const&)>;
  using OnTransferProgressCallback = std::function<void(ContactID const&, std::string const&, float)>;
  using OnAttachmentStatusChangedCallback =
      std::function<void(ContactID const&, std::string const&, messaging::AttachmentStatus)>;

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
    size_t window_end = kWindowSizeChunks;
    size_t total_size = 0;
    bool paused = false;
  };

  struct TransferDownloadState {
    std::filesystem::path file_path;
    std::fstream stream;
    std::filesystem::path temp_path;
    uint64_t total_size = 0;
    std::string expected_hash;
    uint64_t message_id = 0;
    size_t highest_seq_written = 0;
    size_t bytes_written = 0;
  };

  void StartUploadInternal(ContactID const& peer_id, messaging::Attachment const& att) {
    if (active_uploads_.contains(att.id)) {
      active_uploads_[att.id].paused = false;
      PumpUpload(peer_id, att.id, active_uploads_[att.id]);
    } else {
      std::ifstream file(att.file_path, std::ios::binary);
      if (!file) {
        storage_->UpdateAttachmentStatus(att.id, messaging::AttachmentStatus::failed, true);
        return;
      }

      // Update status to Transferring BEFORE starting the pump.
      // If PumpUpload finishes synchronously (small files), it will trigger 'Done'.
      // Doing this first prevents 'Transferring' from overwriting 'Done'.
      storage_->UpdateAttachmentStatus(att.id, messaging::AttachmentStatus::transferring, true);
      INVARIANT(on_attachment_status_changed_);
      on_attachment_status_changed_(peer_id, att.id, messaging::AttachmentStatus::transferring);

      TransferUploadState state;
      state.file_path = att.file_path;
      state.stream = std::move(file);
      state.total_size = att.size_bytes;
      state.sequence = 0;
      state.window_end = kWindowSizeChunks;
      active_uploads_[att.id] = std::move(state);

      PumpUpload(peer_id, att.id, active_uploads_[att.id]);
    }
  }

  void HandleResume(ContactID const& peer_id, std::string const& file_id) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_downloads_.contains(file_id)) {
      auto& state = active_downloads_[file_id];
      if (!state.stream.is_open()) {
        state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
      }
      SendAck(peer_id, file_id, state.highest_seq_written);
    } else if (active_uploads_.contains(file_id)) {
      // Just ensure not paused and try pump
      active_uploads_[file_id].paused = false;
      PumpUpload(peer_id, file_id, active_uploads_[file_id]);
    }
  }

  void HandleAck(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < 4) return;
    uint32_t next_seq = 0;
    std::memcpy(&next_seq, payload.data(), 4);

    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    auto it = active_uploads_.find(file_id);
    if (it != active_uploads_.end()) {
      auto& state = it->second;
      if (next_seq > state.sequence) state.sequence = next_seq;
      state.window_end = next_seq + kWindowSizeChunks;
      PumpUpload(peer_id, file_id, state);
    }
  }

  void PumpUpload(ContactID const& peer_id, std::string const& file_id, TransferUploadState& state) {
    while (state.sequence < state.window_end) {
      size_t offset = state.sequence * kChunkSizeBytes;
      if (offset >= state.total_size && state.total_size > 0) break;

      state.stream.clear();
      state.stream.seekg(offset);
      if (state.stream.fail()) break;

      std::vector<std::byte> buffer(kChunkSizeBytes + 200);
      size_t id_len = file_id.size();
      buffer[0] = std::byte{0xFE};
      buffer[1] = std::byte{static_cast<uint8_t>(MsgType::Chunk)};
      buffer[2] = std::byte{static_cast<uint8_t>(id_len)};
      std::memcpy(buffer.data() + 3, file_id.data(), id_len);

      size_t header_size = 3 + id_len;
      uint32_t seq_net = state.sequence;
      std::memcpy(buffer.data() + header_size, &seq_net, 4);
      header_size += 4;

      state.stream.read(reinterpret_cast<char*>(buffer.data() + header_size), kChunkSizeBytes);
      size_t bytes_read = state.stream.gcount();
      if (bytes_read == 0 && state.stream.eof()) break;

      buffer.resize(header_size + bytes_read);

      if (!transport_->SendPeerDataChannelBinaryMessage(peer_id, buffer)) {
        // Backpressure
        break;
      }

      if (on_transfer_progress_ && state.total_size > 0) {
        float p = (float)(offset + bytes_read) / state.total_size;
        on_transfer_progress_(peer_id, file_id, p > 1.0f ? 1.0f : p);
      }

      state.sequence++;
    }

    if ((state.sequence * kChunkSizeBytes) >= state.total_size) {
      if (SendEnd(peer_id, file_id)) {
        active_uploads_.erase(file_id);
        storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);
        if (on_attachment_status_changed_)
          on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
      }
    }
  }

  void HandleChunk(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < 4) return;
    uint32_t seq = 0;
    std::memcpy(&seq, payload.data(), 4);

    std::span<std::byte const> chunk_data = payload.subspan(4);

    std::unique_lock<std::mutex> lock(transfers_mutex_);

    if (!active_downloads_.contains(file_id)) {
      auto att = storage_->GetAttachment(file_id);
      if (att && att->status != messaging::AttachmentStatus::canceled) {
        lock.unlock();
        QueueDownload(peer_id, *att, 0);
        lock.lock();
      }
    }

    auto it = active_downloads_.find(file_id);
    if (it == active_downloads_.end()) return;

    auto& state = it->second;
    if (state.stream.is_open()) {
      size_t offset = seq * kChunkSizeBytes;

      // Ensure not seeking past EOF if we didn't resize correctly
      state.stream.seekp(offset);
      state.stream.write(reinterpret_cast<char const*>(chunk_data.data()), chunk_data.size());

      if (seq + 1 > state.highest_seq_written) {
        state.highest_seq_written = seq + 1;
      }

      // Simple bytes written tracking for ordered delivery
      // Since we write randomly, this is approximate, assuming reliable ordered delivery.
      // If we get chunks out of order, this counter is insufficient for "completeness".
      // But DataChannels are ordered. So simple addition is fine.
      state.bytes_written += chunk_data.size();

      if (on_transfer_progress_ && state.total_size > 0) {
        float p = (float)state.bytes_written / state.total_size;
        if (lock.owns_lock()) lock.unlock();
        on_transfer_progress_(peer_id, file_id, p > 1.0f ? 1.0f : p);
        lock.lock();
      }

      if (seq % (kWindowSizeChunks / 2) == 0) {
        SendAck(peer_id, file_id, state.highest_seq_written);
      }
    }
  }

  void HandleEnd(ContactID const& peer_id, std::string const& file_id) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto it = active_downloads_.find(file_id);
    if (it == active_downloads_.end()) return;

    auto& state = it->second;
    state.stream.flush();

    if (state.bytes_written < state.total_size) {
      LGE << "Incomplete file received for " << file_id << ". Expected " << state.total_size << ", got "
          << state.bytes_written;
      // Just ACK highest sequence to request more
      SendAck(peer_id, file_id, state.highest_seq_written);
      return;
    }

    state.stream.close();
    auto temp_path = state.temp_path;
    auto final_path = state.file_path;
    auto expected = state.expected_hash;
    active_downloads_.erase(it);
    lock.unlock();

    std::string calc_hash = crypto::CalculateFileHash(temp_path);
    if (calc_hash == expected) {
      std::filesystem::rename(temp_path, final_path);
      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);
      if (on_attachment_downloaded_) on_attachment_downloaded_(peer_id, file_id);
      if (on_transfer_progress_) on_transfer_progress_(peer_id, file_id, 1.0f);
      if (on_attachment_status_changed_)
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
    } else {
      LGE << "Hash mismatch for " << file_id << ". Calc: " << calc_hash << " Exp: " << expected;
      std::filesystem::remove(temp_path);
      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::failed, false);
      if (on_attachment_status_changed_)
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::failed);
    }
  }

  void HandleCancel(ContactID const& peer_id, std::string const& file_id) {
    CancelTransfer(peer_id, file_id, active_uploads_.contains(file_id), false);
  }

  bool
  SendPacket(ContactID const& peer_id, std::string const& file_id, MsgType type, std::span<std::byte const> payload) {
    std::vector<std::byte> buf(3 + file_id.size() + payload.size());
    buf[0] = std::byte{0xFE};
    buf[1] = std::byte{static_cast<uint8_t>(type)};
    buf[2] = std::byte{static_cast<uint8_t>(file_id.size())};
    std::memcpy(buf.data() + 3, file_id.data(), file_id.size());
    if (!payload.empty()) {
      std::memcpy(buf.data() + 3 + file_id.size(), payload.data(), payload.size());
    }
    return transport_->SendPeerDataChannelBinaryMessage(peer_id, buf);
  }

  void SendResumeRequest(ContactID const& peer_id, std::string const& file_id) {
    SendPacket(peer_id, file_id, MsgType::Resume, {});
  }

  void SendCancel(ContactID const& peer_id, std::string const& file_id) {
    SendPacket(peer_id, file_id, MsgType::Cancel, {});
  }

  bool SendEnd(ContactID const& peer_id, std::string const& file_id) {
    return SendPacket(peer_id, file_id, MsgType::End, {});
  }

  void SendAck(ContactID const& peer_id, std::string const& file_id, uint32_t next_seq) {
    std::vector<std::byte> payload(4);
    std::memcpy(payload.data(), &next_seq, 4);
    SendPacket(peer_id, file_id, MsgType::Ack, payload);
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
