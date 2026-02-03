#pragma once

#include <chrono>
#include <cstdint>
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
  static constexpr std::size_t chunkSizeBytes = 16ULL * 1024;  // 16KB per chunk
  static constexpr std::size_t windowSizeChunks = 16;

  explicit FileTransferService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)) {
  }

  void OnConnectionReady(ContactID const& peer_id) {
    auto const pending_attachments = storage_->GetPendingAttachments(peer_id);
    for (auto const& attachment : pending_attachments) {
      if (attachment.status == messaging::AttachmentStatus::paused ||
          attachment.status == messaging::AttachmentStatus::canceled ||
          attachment.status == messaging::AttachmentStatus::peerCanceled) {
        continue;
      }

      if (attachment.is_downloaded) {
        // We are uploader
        std::lock_guard<std::mutex> const lock(transfers_mutex_);
        if (!active_uploads_.contains(attachment.id)) {
          // Check if we should really start.
          // If connection just established, we don't push until requested or if it was interrupted transferring.
          // However, for robustness, Uploader stays passive in "resume" scenarios usually.
          // We rely on "transferring" status to indicate intent, but we wait for request.
          // EXCEPT if we are fresh sending (handled by QueueUpload).
        }
      } else {
        // We are downloader
        ResumeTransfer(peer_id, attachment.id, false);
      }
    }
  }

  void QueueUpload(ContactID const& peer_id, messaging::Attachment const& attachment) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    StartUploadInternal(peer_id, attachment);
  }

  void QueueDownload(ContactID const& /*peer_id*/, messaging::Attachment const& attachment, std::uint64_t message_id) {
    std::unique_lock<std::mutex> const lock(transfers_mutex_);
    if (active_downloads_.contains(attachment.id)) {
      return;
    }

    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    std::error_code error_code;
    std::filesystem::create_directories(attachments_dir, error_code);
    if (error_code) {
      LGE << "Failed to create attachments directory: " << error_code.message();
      return;
    }

    std::filesystem::path const temp_path = attachments_dir / (attachment.id + ".tmp");
    std::filesystem::path const progress_path = attachments_dir / (attachment.id + ".progress");

    TransferDownloadState state;
    state.file_path = std::filesystem::path(attachment.file_path);
    state.temp_path = temp_path;
    state.progress_path = progress_path;
    state.expected_hash = attachment.hash;
    state.total_size = attachment.size_bytes;
    state.message_id = message_id;

    // Try to recover state
    if (std::filesystem::exists(progress_path)) {
      std::ifstream pf(progress_path, std::ios::binary);
      if (pf) {
        std::uint32_t saved_seq = 0;
        pf.read(reinterpret_cast<char*>(&saved_seq), sizeof(saved_seq));
        if (pf) {
          state.highest_seq_written = saved_seq;
          // Estimate bytes written. Safe lower bound.
          state.bytes_written = static_cast<std::size_t>(saved_seq) * chunkSizeBytes;
        }
      }
    }

    try {
      if (!std::filesystem::exists(state.temp_path)) {
        std::ofstream output_stream(state.temp_path, std::ios::binary | std::ios::out | std::ios::trunc);
        output_stream.close();
        if (attachment.size_bytes > 0) {
          std::filesystem::resize_file(state.temp_path, attachment.size_bytes);
        }
        state.highest_seq_written = 0;
        state.bytes_written = 0;
      } else {
        if (std::filesystem::file_size(state.temp_path) < attachment.size_bytes) {
          std::filesystem::resize_file(state.temp_path, attachment.size_bytes);
        }
      }
    } catch (std::exception const& e) {
      LGE << "Failed to reserve space for attachment " << attachment.id << ": " << e.what();
    }

    state.stream.open(state.temp_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!state.stream.is_open()) {
      LGE << "Failed to open temp file for download: " << state.temp_path.string();
      return;
    }

    active_downloads_[attachment.id] = std::move(state);
  }

  enum class MessageType : std::uint8_t {
    chunk = 0x01,
    ack = 0x02,
    cancel = 0x03,
    pause = 0x04,
    resume = 0x05,
    end = 0x06
  };

  void ProcessBinaryMessage(ContactID const& peer_id, std::span<std::byte const> data) {
    if (data.size() < headerMinSize) {
      return;
    }
    if (data[0] != magicByte) {
      return;
    }

    auto const type = static_cast<MessageType>(data[1]);
    auto const id_length = static_cast<std::uint8_t>(data[2]);

    if (data.size() < headerMinSize + id_length) {
      return;
    }

    std::string file_id;
    file_id.resize(id_length);
    std::memcpy(file_id.data(), data.data() + headerMinSize, id_length);

    std::span<std::byte const> const payload = data.subspan(headerMinSize + id_length);

    switch (type) {
      case MessageType::chunk:
        HandleChunk(peer_id, file_id, payload);
        break;
      case MessageType::ack:
        HandleAck(peer_id, file_id, payload);
        break;
      case MessageType::cancel:
        HandleCancel(peer_id, file_id);
        break;
      case MessageType::pause:
        HandlePause(peer_id, file_id);
        break;
      case MessageType::resume:
        HandleResume(peer_id, file_id, payload);
        break;
      case MessageType::end:
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
        active_downloads_.erase(attachment_id);
      }
    }

    if (notify_peer) {
      if (is_upload) {
        SendCancel(peer_id, attachment_id);
      } else {
        // Downloader "Cancel" acts as Pause unless Discard is called.
        SendPause(peer_id, attachment_id);
      }
    }

    // Download cancellation -> Pause local state
    auto const new_status = (!is_upload) ? messaging::AttachmentStatus::paused : messaging::AttachmentStatus::canceled;

    storage_->UpdateAttachmentStatus(attachment_id, new_status, is_upload);
    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, attachment_id, new_status);
    }
  }

  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload) {
    if (is_upload) {
      // Sender Side Resume:
      // We do NOT start pumping data immediately to avoid forcing data on a paused receiver.
      // We set status to Transferring so UI reflects readiness.
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::transferring, true);
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::transferring);
      }
    } else {
      // Receiver Side Resume:
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::transferring, false);
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::transferring);
      }

      // Check where we left off
      std::uint32_t start_seq = 0;
      std::filesystem::path const storage_root(storage_->GetPath());
      std::filesystem::path const progress_path =
          storage_root.parent_path() / "attachments" / (attachment_id + ".progress");

      if (std::filesystem::exists(progress_path)) {
        std::ifstream pf(progress_path, std::ios::binary);
        if (pf) {
          pf.read(reinterpret_cast<char*>(&start_seq), sizeof(start_seq));
        }
      }

      SendResumeRequest(peer_id, attachment_id, start_seq);
    }
  }

  void DiscardAttachment(ContactID const& peer_id, std::string const& attachment_id) {
    auto attachment = storage_->GetAttachment(attachment_id);
    bool should_hide = false;

    if (attachment) {
      if (attachment->status == messaging::AttachmentStatus::canceled ||
          attachment->status == messaging::AttachmentStatus::peerCanceled ||
          attachment->status == messaging::AttachmentStatus::failed) {
        should_hide = true;
      }
    }

    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      active_uploads_.erase(attachment_id);
      if (active_downloads_.contains(attachment_id)) {
        auto& state = active_downloads_[attachment_id];
        state.stream.close();
        if (should_hide) {
          std::error_code error_code;
          std::filesystem::remove(state.temp_path, error_code);
          std::filesystem::remove(state.progress_path, error_code);
        }
        active_downloads_.erase(attachment_id);
      }
    }

    if (!should_hide) {
      // Explicitly send CANCEL (0x03) to stop completely
      SendCancel(peer_id, attachment_id);
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, false);
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::canceled);
      }
    } else {
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::discarded, false);
      if (on_attachment_status_changed_) {
        on_attachment_status_changed_(peer_id, attachment_id, messaging::AttachmentStatus::discarded);
      }
    }
  }

  using OnAttachmentDownloadedCallback = std::function<void(ContactID const&, std::string const&)>;
  using OnTransferProgressCallback = std::function<void(ContactID const&, std::string const&, float)>;
  using OnAttachmentStatusChangedCallback =
      std::function<void(ContactID const&, std::string const&, messaging::AttachmentStatus)>;

  void SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb) {
    on_attachment_downloaded_ = std::move(cb);
  }
  void SetTransferProgressCallback(OnTransferProgressCallback cb) {
    on_transfer_progress_ = std::move(cb);
  }
  void SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb) {
    on_attachment_status_changed_ = std::move(cb);
  }

  // Exposed for InternalMessagingService to route "file_cancel" JSON messages
  void HandleCancel(ContactID const& peer_id, std::string const& file_id) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_uploads_.contains(file_id)) {
      active_uploads_.erase(file_id);
    }
    if (active_downloads_.contains(file_id)) {
      auto& state = active_downloads_[file_id];
      state.stream.close();
      active_downloads_.erase(file_id);
    }

    auto attachment = storage_->GetAttachment(file_id);
    if (attachment && attachment->status == messaging::AttachmentStatus::discarded) {
      return;
    }

    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::peerCanceled, false);
    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::peerCanceled);
    }
  }

 private:
  static constexpr std::byte magicByte{0xFE};
  static constexpr std::size_t headerMinSize = 3;
  static constexpr std::size_t sequenceSize = 4;

  struct TransferUploadState {
    std::string file_path;
    std::ifstream stream;
    std::size_t sequence = 0;
    std::size_t window_end = windowSizeChunks;
    std::size_t total_size = 0;
    bool paused = false;
  };

  struct TransferDownloadState {
    std::filesystem::path file_path;
    std::fstream stream;
    std::filesystem::path temp_path;
    std::filesystem::path progress_path;
    std::uint64_t total_size = 0;
    std::string expected_hash;
    std::uint64_t message_id = 0;
    std::size_t highest_seq_written = 0;
    std::size_t bytes_written = 0;
  };

  void StartUploadInternal(
      ContactID const& peer_id,
      messaging::Attachment const& attachment,
      std::uint32_t start_sequence = 0
  ) {
    if (active_uploads_.contains(attachment.id)) {
      auto& state = active_uploads_[attachment.id];
      if (start_sequence > 0) {
        state.sequence = start_sequence;
        state.window_end = state.sequence + windowSizeChunks;
      }
      state.paused = false;
      PumpUpload(peer_id, attachment.id, state);
    } else {
      std::ifstream file(attachment.file_path, std::ios::binary);
      if (!file) {
        storage_->UpdateAttachmentStatus(attachment.id, messaging::AttachmentStatus::failed, true);
        return;
      }

      storage_->UpdateAttachmentStatus(attachment.id, messaging::AttachmentStatus::transferring, true);
      INVARIANT(on_attachment_status_changed_);
      on_attachment_status_changed_(peer_id, attachment.id, messaging::AttachmentStatus::transferring);

      TransferUploadState state;
      state.file_path = attachment.file_path;
      state.stream = std::move(file);
      state.total_size = attachment.size_bytes;
      state.sequence = start_sequence;
      state.window_end = start_sequence + windowSizeChunks;
      active_uploads_[attachment.id] = std::move(state);

      PumpUpload(peer_id, attachment.id, active_uploads_[attachment.id]);
    }
  }

  void HandlePause(ContactID const& peer_id, std::string const& file_id) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_uploads_.contains(file_id)) {
      active_uploads_.erase(file_id);
    }
    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::paused, true);
    if (on_attachment_status_changed_) {
      on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::paused);
    }
  }

  void HandleResume(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    std::uint32_t requested_seq = 0;
    if (payload.size() >= sizeof(requested_seq)) {
      std::memcpy(&requested_seq, payload.data(), sizeof(requested_seq));
    }

    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    if (active_downloads_.contains(file_id)) {
      // Ignored for now
    } else if (active_uploads_.contains(file_id)) {
      auto& state = active_uploads_[file_id];
      if (requested_seq > 0) {
        state.sequence = requested_seq;
        state.window_end = requested_seq + windowSizeChunks;
      }
      state.paused = false;
      PumpUpload(peer_id, file_id, active_uploads_[file_id]);
    } else {
      auto attachment = storage_->GetAttachment(file_id);
      if (attachment && attachment->status != messaging::AttachmentStatus::discarded) {
        StartUploadInternal(peer_id, *attachment, requested_seq);
      }
    }
  }

  void HandleAck(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < sequenceSize) {
      return;
    }
    std::uint32_t next_seq = 0;
    std::memcpy(&next_seq, payload.data(), sizeof(next_seq));

    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    auto iterator = active_uploads_.find(file_id);
    if (iterator != active_uploads_.end()) {
      auto& state = iterator->second;
      if (next_seq > state.sequence) {
        state.sequence = next_seq;
      }
      state.window_end = next_seq + windowSizeChunks;
      PumpUpload(peer_id, file_id, state);
    }
  }

  void PumpUpload(ContactID const& peer_id, std::string const& file_id, TransferUploadState& state) {
    while (state.sequence < state.window_end) {
      std::size_t const offset = state.sequence * chunkSizeBytes;
      if (offset >= state.total_size && state.total_size > 0) {
        break;
      }

      if (!state.stream.good()) {
        state.stream.clear();
      }
      state.stream.seekg(static_cast<std::streamoff>(offset));
      if (state.stream.fail()) {
        break;
      }

      std::size_t const id_len = file_id.size();
      std::size_t const header_size = headerMinSize + id_len + sequenceSize;
      std::vector<std::byte> buffer(header_size + chunkSizeBytes);

      buffer[0] = magicByte;
      buffer[1] = static_cast<std::byte>(MessageType::chunk);
      buffer[2] = static_cast<std::byte>(id_len);
      std::memcpy(buffer.data() + headerMinSize, file_id.data(), id_len);

      auto const seq_net = static_cast<std::uint32_t>(state.sequence);
      std::memcpy(buffer.data() + headerMinSize + id_len, &seq_net, sizeof(seq_net));

      state.stream.read(reinterpret_cast<char*>(buffer.data() + header_size), chunkSizeBytes);
      auto const bytes_read = static_cast<std::size_t>(state.stream.gcount());
      if (bytes_read == 0 && state.stream.eof()) {
        break;
      }

      buffer.resize(header_size + bytes_read);

      if (!transport_->SendPeerDataChannelBinaryMessage(peer_id, buffer)) {
        break;
      }

      if (on_transfer_progress_ && state.total_size > 0) {
        auto const progress = static_cast<float>(offset + bytes_read) / static_cast<float>(state.total_size);
        on_transfer_progress_(peer_id, file_id, std::min(progress, 1.0F));
      }

      state.sequence++;
    }

    if ((state.sequence * chunkSizeBytes) >= state.total_size) {
      if (SendEnd(peer_id, file_id)) {
        active_uploads_.erase(file_id);
        storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);
        if (on_attachment_status_changed_) {
          on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
        }
      }
    }
  }

  void HandleChunk(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < sequenceSize) {
      return;
    }
    std::uint32_t sequence = 0;
    std::memcpy(&sequence, payload.data(), sizeof(sequence));

    std::span<std::byte const> const chunk_data = payload.subspan(sequenceSize);

    std::unique_lock<std::mutex> lock(transfers_mutex_);

    if (!active_downloads_.contains(file_id)) {
      auto attachment = storage_->GetAttachment(file_id);

      if (attachment) {
          // Guard against forced push if we paused/canceled
          if (attachment->status == messaging::AttachmentStatus::paused ||
              attachment->status == messaging::AttachmentStatus::canceled ||
              attachment->status == messaging::AttachmentStatus::peerCanceled ||
              attachment->status == messaging::AttachmentStatus::discarded) {
              return;
          }

          if (attachment->status != messaging::AttachmentStatus::done) {
            lock.unlock();
            QueueDownload(peer_id, *attachment, 0);
            lock.lock();
          }
      } else {
          return;
      }
    }

    auto iterator = active_downloads_.find(file_id);
    if (iterator == active_downloads_.end()) {
      return;
    }

    auto& state = iterator->second;
    if (state.stream.is_open()) {
      auto const offset = sequence * chunkSizeBytes;

      state.stream.seekp(offset);
      state.stream.write(
          reinterpret_cast<char const*>(chunk_data.data()),
          static_cast<std::streamsize>(chunk_data.size())
      );

      if (sequence + 1 > state.highest_seq_written) {
        state.highest_seq_written = sequence + 1;

        // Save progress to disk for resume
        std::ofstream pf(state.progress_path, std::ios::binary);
        if (pf) {
            std::uint32_t const next = static_cast<std::uint32_t>(state.highest_seq_written);
            pf.write(reinterpret_cast<char const*>(&next), sizeof(next));
        }
      }

      state.bytes_written += chunk_data.size();

      if (on_transfer_progress_ && state.total_size > 0) {
        auto const progress = static_cast<float>(state.bytes_written) / static_cast<float>(state.total_size);
        if (lock.owns_lock()) {
          lock.unlock();
        }
        on_transfer_progress_(peer_id, file_id, std::min(progress, 1.0F));
        lock.lock();
      }

      if (sequence % (windowSizeChunks / 2) == 0) {
        SendAck(peer_id, file_id, static_cast<std::uint32_t>(state.highest_seq_written));
      }
    }
  }

  void HandleEnd(ContactID const& peer_id, std::string const& file_id) {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto iterator = active_downloads_.find(file_id);
    if (iterator == active_downloads_.end()) {
      return;
    }

    auto& state = iterator->second;
    state.stream.flush();

    if ((state.highest_seq_written * chunkSizeBytes) < state.total_size) {
      SendAck(peer_id, file_id, static_cast<std::uint32_t>(state.highest_seq_written));
      return;
    }

    state.stream.close();
    auto temp_path = state.temp_path;
    auto final_path = state.file_path;
    auto expected = state.expected_hash;
    auto progress_path = state.progress_path;

    active_downloads_.erase(iterator);
    lock.unlock();

    std::string const calc_hash = crypto::CalculateFileHash(temp_path);
    if (calc_hash == expected) {
      std::error_code error_code;
      std::filesystem::rename(temp_path, final_path, error_code);
      std::filesystem::remove(progress_path, error_code);
      if (error_code) {
        LGE << "Failed to rename temp file: " << error_code.message();
      } else {
        storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);
        if (on_attachment_downloaded_) on_attachment_downloaded_(peer_id, file_id);
        if (on_transfer_progress_) on_transfer_progress_(peer_id, file_id, 1.0F);
        if (on_attachment_status_changed_)
          on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::done);
      }
    } else {
      LGE << "Hash mismatch for " << file_id;
      std::filesystem::remove(temp_path);
      std::filesystem::remove(progress_path);
      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::failed, false);
      if (on_attachment_status_changed_)
        on_attachment_status_changed_(peer_id, file_id, messaging::AttachmentStatus::failed);
    }
  }

  auto SendPacket(
      ContactID const& peer_id,
      std::string const& file_id,
      MessageType type,
      std::span<std::byte const> payload
  ) -> bool {
    std::size_t const id_len = file_id.size();
    std::size_t const header_size = headerMinSize + id_len;
    std::vector<std::byte> buf(header_size + payload.size());

    buf[0] = magicByte;
    buf[1] = static_cast<std::byte>(type);
    buf[2] = static_cast<std::byte>(id_len);
    std::memcpy(buf.data() + headerMinSize, file_id.data(), id_len);

    if (!payload.empty()) {
      std::memcpy(buf.data() + header_size, payload.data(), payload.size());
    }
    return transport_->SendPeerDataChannelBinaryMessage(peer_id, buf);
  }

  void SendResumeRequest(ContactID const& peer_id, std::string const& file_id, std::uint32_t start_seq) {
    std::vector<std::byte> payload(sizeof(start_seq));
    std::memcpy(payload.data(), &start_seq, sizeof(start_seq));
    SendPacket(peer_id, file_id, MessageType::resume, payload);
  }

  void SendPause(ContactID const& peer_id, std::string const& file_id) {
    SendPacket(peer_id, file_id, MessageType::pause, {});
  }

  void SendCancel(ContactID const& peer_id, std::string const& file_id) {
    SendPacket(peer_id, file_id, MessageType::cancel, {});
  }

  auto SendEnd(ContactID const& peer_id, std::string const& file_id) -> bool {
    return SendPacket(peer_id, file_id, MessageType::end, {});
  }

  void SendAck(ContactID const& peer_id, std::string const& file_id, std::uint32_t next_seq) {
    std::vector<std::byte> payload(sizeof(next_seq));
    std::memcpy(payload.data(), &next_seq, sizeof(next_seq));
    SendPacket(peer_id, file_id, MessageType::ack, payload);
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
