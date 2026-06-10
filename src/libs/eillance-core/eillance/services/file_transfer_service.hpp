#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
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
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

class FileTransferService {
 public:
  static constexpr std::size_t chunkSizeBytes = 16ULL * 1024;

  explicit FileTransferService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelMessage>([this](auto const& e) -> void {
      if (!e.message.empty() && e.message[0] == std::byte{0xFE}) {
        ContactID master_id = transport_->GetMasterId(e.peer_id);
        ProcessBinaryMessage(master_id, e.message);
      }
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSendQueuedMessages>([this](auto const& e) -> void {
      this->OnConnectionReady(e.peer_id);
    }));

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataPeerConnectionStateChanged>([this](auto const& e) -> void {
          if (e.state == transport::PeerConnectionState::connected) {
            this->OnConnectionReady(e.peer_id);
          } else if (e.state == transport::PeerConnectionState::disconnected ||
                     e.state == transport::PeerConnectionState::failed ||
                     e.state == transport::PeerConnectionState::closed) {
            std::lock_guard<std::mutex> const lock(transfers_mutex_);
            for (auto it = active_uploads_.begin(); it != active_uploads_.end();) {
              if (it->second.peer_id == e.peer_id) {
                it = active_uploads_.erase(it);
              } else {
                ++it;
              }
            }
            for (auto it = active_downloads_.begin(); it != active_downloads_.end();) {
              if (it->second.peer_id == e.peer_id) {
                it->second.stream.close();
                it = active_downloads_.erase(it);
              } else {
                ++it;
              }
            }
            std::erase_if(upload_queue_, [&](auto const& q) -> bool { return q.peer_id == e.peer_id; });
            std::erase_if(download_queue_, [&](auto const& q) -> bool { return q.peer_id == e.peer_id; });
          }
        })
    );
  }

  ~FileTransferService() {
    for (auto const& evnt : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(evnt);
    }
  }

  void OnConnectionReady(ContactID const& peer_id) {
    auto pending_attachments = storage_->GetPendingAttachments(TopicID(peer_id));
    for (auto const& g : storage_->GetGroups()) {
      if (std::ranges::any_of(g.members, [&](auto const& m) { return m.contact_id == peer_id; })) {
        auto g_atts = storage_->GetPendingAttachments(TopicID(g.id));
        pending_attachments.insert(pending_attachments.end(), g_atts.begin(), g_atts.end());
      }
    }
    for (auto const& c : storage_->GetChannels()) {
      if (std::ranges::any_of(c.members, [&](auto const& m) { return m.contact_id == peer_id; })) {
        auto c_atts = storage_->GetPendingAttachments(TopicID(c.id));
        pending_attachments.insert(pending_attachments.end(), c_atts.begin(), c_atts.end());
      }
    }

    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    for (auto const& attachment : pending_attachments) {
      if (attachment.status == messaging::AttachmentStatus::paused ||
          attachment.status == messaging::AttachmentStatus::canceled ||
          attachment.status == messaging::AttachmentStatus::peerCanceled) {
        continue;
      }

      auto const key = peer_id.ToString() + "_" + attachment.id;
      if (attachment.is_downloaded) {
        if (!active_uploads_.contains(key)) {
          bool found = false;
          for (auto const& q : upload_queue_) {
            if (q.attachment_id == attachment.id && q.peer_id == peer_id) {
              found = true;
              break;
            }
          }
          if (!found) {
            upload_queue_.push_back({peer_id, attachment.id, 0});
          }
        }
      } else {
        if (!active_downloads_.contains(key)) {
          bool found = false;
          for (auto const& q : download_queue_) {
            if (q.attachment_id == attachment.id && q.peer_id == peer_id) {
              found = true;
              break;
            }
          }
          if (!found) {
            download_queue_.push_back({peer_id, attachment.id, 0});
          }
        }
      }
    }
    TriggerNextTransfers();
  }

  void QueueUpload(ContactID const& peer_id, messaging::Attachment const& attachment) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    upload_queue_.push_back({peer_id, attachment.id, 0});
    TriggerNextTransfers();
  }

  void QueueDownload(ContactID const& peer_id, messaging::Attachment const& attachment, std::uint64_t message_id) {
    std::lock_guard<std::mutex> const lock(transfers_mutex_);
    download_queue_.push_back({peer_id, attachment.id, message_id});
    TriggerNextTransfers();
  }

  void TriggerNextTransfers() {
    transfer_queue_.Dispatch([this]() -> void {
      TryStartNextUpload();
      TryStartNextDownload();
    });
  }

  void TryStartNextUpload() {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (!active_uploads_.empty()) {
      return;
    }

    while (!upload_queue_.empty()) {
      auto [peer_id, attachment_id, message_id] = upload_queue_.front();
      upload_queue_.pop_front();

      auto const stored = storage_->GetAttachment(attachment_id);
      if (!stored || stored->status == messaging::AttachmentStatus::canceled ||
          stored->status == messaging::AttachmentStatus::discarded ||
          stored->status == messaging::AttachmentStatus::peerCanceled ||
          stored->status == messaging::AttachmentStatus::paused ||
          stored->status == messaging::AttachmentStatus::done) {
        continue;
      }

      lock.unlock();
      StartUploadInternal(peer_id, *stored);
      return;
    }
  }

  void TryStartNextDownload() {
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (!active_downloads_.empty()) {
      return;
    }

    while (!download_queue_.empty()) {
      auto [peer_id, attachment_id, message_id] = download_queue_.front();
      download_queue_.pop_front();

      auto const stored = storage_->GetAttachment(attachment_id);
      if (!stored || stored->status == messaging::AttachmentStatus::canceled ||
          stored->status == messaging::AttachmentStatus::discarded ||
          stored->status == messaging::AttachmentStatus::peerCanceled ||
          stored->status == messaging::AttachmentStatus::paused ||
          stored->status == messaging::AttachmentStatus::done) {
        continue;
      }

      lock.unlock();
      StartDownloadInternal(peer_id, *stored, message_id, true);
      return;
    }
  }

  enum class MessageType : std::uint8_t {
    chunk = 0x01,
    ack = 0x02,
    cancel = 0x03,
    pause = 0x04,
    resume = 0x05,
    end = 0x06,
    missing = 0x07,
    done = 0x08
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
      case MessageType::missing:
        HandleMissing(peer_id, file_id, payload);
        break;
      case MessageType::done:
        HandleDone(peer_id, file_id);
        break;
    }
  }

  void CancelTransfer(std::string const& attachment_id, bool const is_upload, bool const notify_peer = true) {
    std::vector<ContactID> peers_to_notify;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (is_upload) {
        for (auto it = active_uploads_.begin(); it != active_uploads_.end();) {
          if (it->second.file_id == attachment_id) {
            peers_to_notify.push_back(it->second.peer_id);
            it = active_uploads_.erase(it);
          } else {
            ++it;
          }
        }
        std::erase_if(upload_queue_, [&](auto const& q) { return q.attachment_id == attachment_id; });
      } else {
        for (auto it = active_downloads_.begin(); it != active_downloads_.end();) {
          if (it->second.file_id == attachment_id) {
            it->second.stream.close();
            peers_to_notify.push_back(it->second.peer_id);
            it = active_downloads_.erase(it);
          } else {
            ++it;
          }
        }
        std::erase_if(download_queue_, [&](auto const& q) { return q.attachment_id == attachment_id; });
      }
    }

    if (notify_peer) {
      for (auto const& peer_id : peers_to_notify) {
        if (is_upload)
          SendCancel(peer_id, attachment_id);
        else
          SendPause(peer_id, attachment_id);
      }
    }

    auto const new_status = (!is_upload) ? messaging::AttachmentStatus::paused : messaging::AttachmentStatus::canceled;

    storage_->UpdateAttachmentStatus(attachment_id, new_status, is_upload);
    TriggerNextTransfers();
  }

  void ResumeTransfer(std::string const& attachment_id, bool const is_upload) {
    if (is_upload) {
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::pending, true);
    } else {
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::pending, false);
    }
    TriggerNextTransfers();
  }

  void DiscardAttachment(std::string const& attachment_id) {
    auto const attachment = storage_->GetAttachment(attachment_id);
    bool should_hide = false;

    if (attachment) {
      if (attachment->status == messaging::AttachmentStatus::canceled ||
          attachment->status == messaging::AttachmentStatus::peerCanceled ||
          attachment->status == messaging::AttachmentStatus::failed) {
        should_hide = true;
      }
    }

    std::vector<ContactID> peers_to_notify;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      for (auto it = active_uploads_.begin(); it != active_uploads_.end();) {
        if (it->second.file_id == attachment_id) {
          it = active_uploads_.erase(it);
        } else {
          ++it;
        }
      }
      for (auto it = active_downloads_.begin(); it != active_downloads_.end();) {
        if (it->second.file_id == attachment_id) {
          it->second.stream.close();
          if (should_hide) {
            std::error_code error_code;
            std::filesystem::remove(it->second.temp_path, error_code);
            std::filesystem::remove(it->second.progress_path, error_code);
          }
          peers_to_notify.push_back(it->second.peer_id);
          it = active_downloads_.erase(it);
        } else {
          ++it;
        }
      }
      std::erase_if(upload_queue_, [&](auto const& q) { return q.attachment_id == attachment_id; });
      std::erase_if(download_queue_, [&](auto const& q) { return q.attachment_id == attachment_id; });
    }

    if (!should_hide) {
      for (auto const& peer_id : peers_to_notify) {
        SendCancel(peer_id, attachment_id);
      }
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::canceled, false);
    } else {
      storage_->UpdateAttachmentStatus(attachment_id, messaging::AttachmentStatus::discarded, false);
    }
    TriggerNextTransfers();
  }

  void HandleCancel(ContactID const& peer_id, std::string const& file_id) {
    auto const key = peer_id.ToString() + "_" + file_id;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (active_uploads_.contains(key)) {
        active_uploads_.erase(key);
      }
      if (active_downloads_.contains(key)) {
        auto& state = active_downloads_[key];
        state.stream.close();
        active_downloads_.erase(key);
      }
    }

    if (auto const attachment = storage_->GetAttachment(file_id);
        attachment && attachment->status == messaging::AttachmentStatus::discarded) {
      TriggerNextTransfers();
      return;
    }

    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::peerCanceled, false);
    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{TopicID(peer_id), file_id, messaging::AttachmentStatus::peerCanceled}
    );
    TriggerNextTransfers();
  }

 private:
  static constexpr std::byte magicByte{0xFE};
  static constexpr std::size_t headerMinSize = 3;
  static constexpr std::size_t sequenceSize = 4;

  void FinalizeDownload(ContactID const& peer_id, std::string const& file_id, std::unique_lock<std::mutex>& lock) {
    auto const key = peer_id.ToString() + "_" + file_id;
    auto iterator = active_downloads_.find(key);
    if (iterator == active_downloads_.end()) {
      lock.unlock();
      TriggerNextTransfers();
      return;
    }

    auto& state = iterator->second;
    state.stream.close();
    auto const temp_path = state.temp_path;
    auto const final_path = state.file_path;
    auto const expected = state.expected_hash;
    auto const progress_path = state.progress_path;

    active_downloads_.erase(iterator);
    lock.unlock();

    if (auto const calc_hash = crypto::CalculateFileHash(temp_path); calc_hash == expected) {
      std::error_code error_code;
      std::filesystem::rename(temp_path, final_path, error_code);
      std::filesystem::remove(progress_path, error_code);
      if (error_code) {
        LGE << "Failed to rename temp file: " << error_code.message();
      } else {
        storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, false);
        EventQueue::Instance().Push(EventAttachmentDownloaded{TopicID(peer_id), file_id});
        EventQueue::Instance().Push(EventTransferProgress{TopicID(peer_id), file_id, 1.0F});
        EventQueue::Instance().Push(
            EventAttachmentStatusChanged{TopicID(peer_id), file_id, messaging::AttachmentStatus::done}
        );
      }
    } else {
      LGE << "Hash mismatch for " << file_id;
      std::filesystem::remove(temp_path);
      std::filesystem::remove(progress_path);
      storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::failed, false);
      EventQueue::Instance().Push(
          EventAttachmentStatusChanged{TopicID(peer_id), file_id, messaging::AttachmentStatus::failed}
      );
    }
    TriggerNextTransfers();
  }

  struct TransferUploadState {
    ContactID peer_id;
    std::string file_id;
    std::string file_path;
    std::ifstream stream;
    std::vector<std::uint32_t> pending_chunks;
    std::size_t total_size = 0;
    bool paused = false;
    bool is_pumping = false;
    std::size_t chunks_sent_absolute = 0;
    std::size_t chunks_acked_absolute = 0;
  };

  struct TransferDownloadState {
    ContactID peer_id;
    std::string file_id;
    std::filesystem::path file_path;
    std::fstream stream;
    std::filesystem::path temp_path;
    std::filesystem::path progress_path;
    std::uint64_t total_size = 0;
    std::string expected_hash;
    std::uint64_t message_id = 0;
    std::vector<bool> received_chunks;
    std::size_t bytes_written = 0;
    int chunk_counter = 0;
  };

  void StartDownloadInternal(
      ContactID const& peer_id,
      messaging::Attachment const& attachment,
      std::uint64_t message_id,
      bool request_missing
  ) {
    auto const key = peer_id.ToString() + "_" + attachment.id;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (active_downloads_.contains(key)) {
        return;
      }
    }

    std::filesystem::path const storage_root(storage_->GetPath());
    std::filesystem::path const attachments_dir = storage_root.parent_path() / "attachments";
    std::error_code error_code;
    std::filesystem::create_directories(attachments_dir, error_code);
    if (error_code) {
      LGE << "Failed to create attachments directory: " << error_code.message();
      TriggerNextTransfers();
      return;
    }

    std::filesystem::path const temp_path = attachments_dir / (attachment.id + ".tmp");
    std::filesystem::path const progress_path = attachments_dir / (attachment.id + ".progress");

    TransferDownloadState state;
    state.peer_id = peer_id;
    state.file_id = attachment.id;
    state.file_path = std::filesystem::path(attachment.file_path);
    state.temp_path = temp_path;
    state.progress_path = progress_path;
    state.expected_hash = attachment.hash;
    state.total_size = attachment.size_bytes;
    state.message_id = message_id;

    std::size_t const num_chunks = (attachment.size_bytes + chunkSizeBytes - 1) / chunkSizeBytes;
    state.received_chunks.resize(num_chunks > 0 ? num_chunks : 1, false);
    state.bytes_written = 0;

    if (std::filesystem::exists(progress_path)) {
      if (std::ifstream part_file(progress_path, std::ios::binary); part_file) {
        std::vector<std::uint8_t> packed((state.received_chunks.size() + 7) / 8, 0);
        part_file.read(reinterpret_cast<char*>(packed.data()), packed.size());
        for (size_t i = 0; i < state.received_chunks.size(); ++i) {
          state.received_chunks[i] = (packed[i / 8] & (1 << (i % 8))) != 0;
          if (state.received_chunks[i]) {
            std::size_t chunk_len = chunkSizeBytes;
            if (i == state.received_chunks.size() - 1 && attachment.size_bytes > 0) {
              chunk_len = attachment.size_bytes - (i * chunkSizeBytes);
            }
            state.bytes_written += chunk_len;
          }
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
      TriggerNextTransfers();
      return;
    }

    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (!active_downloads_.contains(key)) {
        active_downloads_[key] = std::move(state);
      }
    }

    storage_->UpdateAttachmentStatus(attachment.id, messaging::AttachmentStatus::transferring, false);
    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{TopicID(peer_id), attachment.id, messaging::AttachmentStatus::transferring}
    );

    if (request_missing) {
      std::vector<std::uint32_t> missing;
      for (std::uint32_t i = 0; i < state.received_chunks.size(); ++i) {
        if (!state.received_chunks[i]) {
          missing.push_back(i);
        }
      }
      SendMissing(peer_id, attachment.id, missing);
    }
  }

  void StartUploadInternal(
      ContactID const& peer_id,
      messaging::Attachment const& attachment,
      std::vector<std::uint32_t> const& missing_chunks = {}
  ) {
    auto const key = peer_id.ToString() + "_" + attachment.id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (active_uploads_.contains(key)) {
      auto& state = active_uploads_[key];
      state.paused = false;
      if (!missing_chunks.empty()) {
        state.pending_chunks = missing_chunks;
        std::reverse(state.pending_chunks.begin(), state.pending_chunks.end());
      }
      if (!state.is_pumping) {
        state.is_pumping = true;
        transfer_queue_.Dispatch([this, peer_id, file_id = attachment.id]() -> void {
          PumpUploadBatch(peer_id, file_id);
        });
      }
    } else {
      std::ifstream file(attachment.file_path, std::ios::binary);
      if (!file) {
        lock.unlock();
        storage_->UpdateAttachmentStatus(attachment.id, messaging::AttachmentStatus::failed, true);
        TriggerNextTransfers();
        return;
      }

      lock.unlock();
      storage_->UpdateAttachmentStatus(attachment.id, messaging::AttachmentStatus::transferring, true);
      EventQueue::Instance().Push(
          EventAttachmentStatusChanged{TopicID(peer_id), attachment.id, messaging::AttachmentStatus::transferring}
      );
      lock.lock();

      TransferUploadState state;
      state.peer_id = peer_id;
      state.file_id = attachment.id;
      state.file_path = attachment.file_path;
      state.stream = std::move(file);
      state.total_size = attachment.size_bytes;

      if (missing_chunks.empty()) {
        std::size_t const num_chunks = (attachment.size_bytes + chunkSizeBytes - 1) / chunkSizeBytes;
        std::size_t const chunks_count = num_chunks > 0 ? num_chunks : 1;
        for (std::uint32_t i = chunks_count; i > 0; --i) {
          state.pending_chunks.push_back(i - 1);
        }
        state.chunks_sent_absolute = 0;
        state.chunks_acked_absolute = 0;
      } else {
        state.pending_chunks = missing_chunks;
        std::ranges::reverse(state.pending_chunks);
        std::size_t const num_chunks = (attachment.size_bytes + chunkSizeBytes - 1) / chunkSizeBytes;
        state.chunks_acked_absolute = num_chunks > missing_chunks.size() ? num_chunks - missing_chunks.size() : 0;
        state.chunks_sent_absolute = state.chunks_acked_absolute;
      }

      state.is_pumping = true;
      active_uploads_[key] = std::move(state);

      transfer_queue_.Dispatch([this, peer_id, file_id = attachment.id]() -> void {
        PumpUploadBatch(peer_id, file_id);
      });
    }
  }

  void HandlePause(ContactID const& peer_id, std::string const& file_id) {
    auto const key = peer_id.ToString() + "_" + file_id;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      if (active_uploads_.contains(key)) {
        active_uploads_.erase(key);
      }
    }
    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::paused, true);
    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{TopicID(peer_id), file_id, messaging::AttachmentStatus::paused}
    );
    TriggerNextTransfers();
  }

  void HandleResume(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> const) {
    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (active_uploads_.contains(key)) {
      auto& state = active_uploads_[key];
      state.paused = false;
      if (!state.is_pumping) {
        state.is_pumping = true;
        transfer_queue_.Dispatch([this, peer_id, file_id]() -> void { PumpUploadBatch(peer_id, file_id); });
      }
    } else {
      auto attachment = storage_->GetAttachment(file_id);
      if (attachment && attachment->status != messaging::AttachmentStatus::discarded) {
        upload_queue_.push_front({peer_id, file_id, 0});
        lock.unlock();
        TriggerNextTransfers();
      }
    }
  }

  void HandleAck(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < sizeof(std::uint64_t)) {
      return;
    }
    std::uint64_t bytes = 0;
    std::memcpy(&bytes, payload.data(), sizeof(bytes));

    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> const lock(transfers_mutex_);
    if (active_uploads_.contains(key)) {
      auto& state = active_uploads_[key];
      if (std::size_t const acked = bytes / chunkSizeBytes; acked > state.chunks_acked_absolute) {
        state.chunks_acked_absolute = acked;
      }

      if (state.total_size > 0) {
        float const progress = static_cast<float>(bytes) / static_cast<float>(state.total_size);
        EventQueue::Instance().Push(EventTransferProgress{TopicID(peer_id), file_id, std::min(progress, 1.0F)});
      }

      if (!state.is_pumping && !state.paused && !state.pending_chunks.empty()) {
        if (constexpr std::size_t max_in_flight = 128;
            state.chunks_sent_absolute < state.chunks_acked_absolute ||
            (state.chunks_sent_absolute - state.chunks_acked_absolute) < max_in_flight) {
          state.is_pumping = true;
          transfer_queue_.Dispatch([this, peer_id, file_id]() -> void { PumpUploadBatch(peer_id, file_id); });
        }
      }
    }
  }

  void ScheduleEndRetry(ContactID const& peer_id, std::string const& file_id) {
    transfer_queue_.DispatchAfter(std::chrono::seconds(5), [this, peer_id, file_id]() -> void {
      auto const key = peer_id.ToString() + "_" + file_id;
      std::unique_lock<std::mutex> lock(transfers_mutex_);
      if (active_uploads_.contains(key)) {
        if (active_uploads_[key].pending_chunks.empty()) {
          return;
        }
        lock.unlock();
        SendEnd(peer_id, file_id);
        ScheduleEndRetry(peer_id, file_id);
      }
    });
  }

  void ScheduleStuckRetry(ContactID const& peer_id, std::string const& file_id) {
    transfer_queue_.DispatchAfter(std::chrono::seconds(3), [this, peer_id, file_id]() -> void {
      auto const key = peer_id.ToString() + "_" + file_id;
      std::unique_lock<std::mutex> lock(transfers_mutex_);
      if (active_uploads_.contains(key)) {
        if (auto const& state = active_uploads_[key];
            !state.is_pumping && !state.paused && !state.pending_chunks.empty()) {
          lock.unlock();
          SendEnd(peer_id, file_id);
          ScheduleStuckRetry(peer_id, file_id);
        }
      }
    });
  }

  void ReadChunksForUpload(
      std::string const& file_id,
      TransferUploadState& state,
      std::size_t batch_size,
      std::vector<std::vector<std::byte>>& buffers_to_send,
      std::vector<std::uint32_t>& seqs_to_send
  ) {
    while (buffers_to_send.size() < batch_size && !state.pending_chunks.empty()) {
      std::uint32_t seq = state.pending_chunks.back();
      state.pending_chunks.pop_back();

      std::size_t const offset = static_cast<std::size_t>(seq) * chunkSizeBytes;
      if (offset >= state.total_size && state.total_size > 0) {
        continue;
      }

      if (!state.stream.good()) {
        state.stream.clear();
      }
      state.stream.seekg(static_cast<std::streamoff>(offset));

      std::size_t bytes_to_read = chunkSizeBytes;
      if (state.total_size == 0) {
        bytes_to_read = 0;
      } else if (offset + chunkSizeBytes > state.total_size) {
        bytes_to_read = state.total_size - offset;
      }

      std::size_t const id_len = file_id.size();
      std::size_t const header_size = headerMinSize + id_len + sequenceSize;
      std::vector<std::byte> buffer(header_size + bytes_to_read);

      buffer[0] = magicByte;
      buffer[1] = static_cast<std::byte>(MessageType::chunk);
      buffer[2] = static_cast<std::byte>(id_len);
      std::memcpy(buffer.data() + headerMinSize, file_id.data(), id_len);

      std::memcpy(buffer.data() + headerMinSize + id_len, &seq, sizeof(seq));

      if (bytes_to_read > 0) {
        state.stream.read(reinterpret_cast<char*>(buffer.data() + header_size), bytes_to_read);
        buffer.resize(header_size + static_cast<std::size_t>(state.stream.gcount()));
      } else {
        buffer.resize(header_size);
      }

      buffers_to_send.push_back(std::move(buffer));
      seqs_to_send.push_back(seq);
      state.chunks_sent_absolute++;
    }
  }

  void HandleUploadSendFailure(
      ContactID const& peer_id,
      std::string const& file_id,
      TransferUploadState& state,
      std::vector<std::uint32_t> const& seqs_to_send,
      std::size_t buffers_to_send_size,
      std::size_t failed_index
  ) {
    for (std::size_t i = buffers_to_send_size; i > failed_index; --i) {
      state.pending_chunks.push_back(seqs_to_send[i - 1]);
      state.chunks_sent_absolute--;
    }
    state.is_pumping = false;

    bool is_connected = false;
    if (auto p = transport_->GetPeer(peer_id)) {
      is_connected = p->data_channel_is_open.load();
    }

    auto const key = peer_id.ToString() + "_" + file_id;
    if (is_connected) {
      transfer_queue_.DispatchAfter(std::chrono::milliseconds(100), [this, peer_id, file_id, key]() -> void {
        {
          std::lock_guard<std::mutex> const retry_lock(transfers_mutex_);
          if (active_uploads_.contains(key)) {
            active_uploads_[key].is_pumping = true;
          }
        }
        PumpUploadBatch(peer_id, file_id);
      });
    } else {
      active_uploads_.erase(key);
    }
  }

  void PumpUploadBatch(ContactID const& peer_id, std::string const& file_id) {
    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto const it = active_uploads_.find(key);
    if (it == active_uploads_.end()) {
      return;
    }
    auto& state = it->second;

    if (state.paused) {
      state.is_pumping = false;
      return;
    }

    if (constexpr std::size_t max_in_flight = 128;
        state.chunks_sent_absolute >= state.chunks_acked_absolute &&
        (state.chunks_sent_absolute - state.chunks_acked_absolute) >= max_in_flight) {
      state.is_pumping = false;
      ScheduleStuckRetry(peer_id, file_id);
      return;
    }

    constexpr std::size_t batch_size = 4;  // 4 chunks of 256KB -> 1MB per loop evaluation
    std::vector<std::vector<std::byte>> buffers_to_send;
    std::vector<std::uint32_t> seqs_to_send;

    ReadChunksForUpload(file_id, state, batch_size, buffers_to_send, seqs_to_send);

    bool const more_pending = !state.pending_chunks.empty();
    lock.unlock();  // Drop lock during heavy asynchronous transfers

    bool send_failed = false;
    std::size_t failed_index = 0;
    for (std::size_t i = 0; i < buffers_to_send.size(); ++i) {
      if (!transport_->SendData(peer_id, buffers_to_send[i])) {
        send_failed = true;
        failed_index = i;
        break;
      }
    }

    lock.lock();
    auto const it2 = active_uploads_.find(key);
    if (it2 == active_uploads_.end()) {
      return;
    }
    auto& state2 = it2->second;

    if (send_failed) {
      HandleUploadSendFailure(peer_id, file_id, state2, seqs_to_send, buffers_to_send.size(), failed_index);
    } else if (more_pending) {
      transfer_queue_.DispatchAfter(std::chrono::milliseconds(10), [this, peer_id, file_id]() -> void {
        PumpUploadBatch(peer_id, file_id);
      });
    } else {
      state2.is_pumping = false;
      SendEnd(peer_id, file_id);
      ScheduleEndRetry(peer_id, file_id);
    }
  }

  void HandleChunk(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    if (payload.size() < sequenceSize) {
      return;
    }
    std::uint32_t sequence = 0;
    std::memcpy(&sequence, payload.data(), sizeof(sequence));

    std::span<std::byte const> const chunk_data = payload.subspan(sequenceSize);

    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);

    if (!active_downloads_.contains(key)) {
      if (auto attachment = storage_->GetAttachment(file_id)) {
        if (attachment->status == messaging::AttachmentStatus::paused) {
          lock.unlock();
          SendPause(peer_id, file_id);
          return;
        }
        if (attachment->status == messaging::AttachmentStatus::canceled ||
            attachment->status == messaging::AttachmentStatus::peerCanceled ||
            attachment->status == messaging::AttachmentStatus::discarded) {
          lock.unlock();
          SendCancel(peer_id, file_id);
          return;
        }
        if (attachment->status == messaging::AttachmentStatus::done) {
          lock.unlock();
          SendDone(peer_id, file_id);
          return;
        }
        lock.unlock();
        StartDownloadInternal(peer_id, *attachment, 0, false);
        lock.lock();
      } else {
        lock.unlock();
        SendCancel(peer_id, file_id);
        return;
      }
    }

    auto const it = active_downloads_.find(key);
    if (it == active_downloads_.end()) {
      return;
    }

    auto& state = it->second;
    if (sequence < state.received_chunks.size()) {
      if (state.received_chunks[sequence]) {
        return;
      }
      state.received_chunks[sequence] = true;
    }

    if (state.stream.is_open()) {
      auto const offset = static_cast<std::size_t>(sequence) * chunkSizeBytes;

      state.stream.seekp(static_cast<std::streamoff>(offset));
      if (!chunk_data.empty()) {
        state.stream.write(
            reinterpret_cast<char const*>(chunk_data.data()),
            static_cast<std::streamsize>(chunk_data.size())
        );
      }
      state.bytes_written += chunk_data.size();
      state.chunk_counter++;

      bool const save_progress = (state.chunk_counter % 8 == 0);
      bool const send_ack = (state.chunk_counter % 16 == 0);
      bool const report_progress = state.total_size > 0;

      std::uint64_t current_bytes = state.bytes_written;
      std::uint64_t total = state.total_size;

      if (save_progress) {
        std::vector<std::uint8_t> packed((state.received_chunks.size() + 7) / 8, 0);
        for (size_t i = 0; i < state.received_chunks.size(); ++i) {
          if (state.received_chunks[i]) {
            packed[i / 8] |= (1 << (i % 8));
          }
        }
        std::filesystem::path const prog_path = state.progress_path;

        lock.unlock();  // Drop lock during saving
        if (std::ofstream file_part(prog_path, std::ios::binary | std::ios::trunc); file_part) {
          file_part.write(reinterpret_cast<char const*>(packed.data()), packed.size());
        }
      } else {
        lock.unlock();
      }

      if (report_progress) {
        float const progress = static_cast<float>(current_bytes) / static_cast<float>(total);
        EventQueue::Instance().Push(EventTransferProgress{TopicID(peer_id), file_id, std::min(progress, 1.0F)});
      }

      if (send_ack) {
        SendAck(peer_id, file_id, current_bytes);
      }
    }
  }

  void HandleEnd(ContactID const& peer_id, std::string const& file_id) {
    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    auto const iterator = active_downloads_.find(key);
    if (iterator == active_downloads_.end()) {
      lock.unlock();
      if (auto const attachment = storage_->GetAttachment(file_id);
          attachment && attachment->status == messaging::AttachmentStatus::done) {
        SendDone(peer_id, file_id);
      } else if (attachment && attachment->status == messaging::AttachmentStatus::paused) {
        SendPause(peer_id, file_id);
      } else {
        SendCancel(peer_id, file_id);
      }
      return;
    }

    auto& state = iterator->second;
    state.stream.flush();

    std::vector<std::uint8_t> packed((state.received_chunks.size() + 7) / 8, 0);
    for (size_t i = 0; i < state.received_chunks.size(); ++i) {
      if (state.received_chunks[i]) {
        packed[i / 8] |= (1 << (i % 8));
      }
    }
    auto const prog_path = state.progress_path;

    std::vector<std::uint32_t> missing;
    for (std::uint32_t i = 0; i < state.received_chunks.size(); ++i) {
      if (!state.received_chunks[i]) {
        missing.push_back(i);
      }
    }

    lock.unlock();

    if (std::ofstream file_part(prog_path, std::ios::binary | std::ios::trunc); file_part) {
      file_part.write(reinterpret_cast<char const*>(packed.data()), packed.size());
    }

    if (missing.empty()) {
      SendDone(peer_id, file_id);
      lock.lock();
      FinalizeDownload(peer_id, file_id, lock);
    } else {
      SendMissing(peer_id, file_id, missing);
    }
  }

  void HandleMissing(ContactID const& peer_id, std::string const& file_id, std::span<std::byte const> payload) {
    std::vector<std::uint32_t> missing;
    if (auto const count = payload.size() / sizeof(std::uint32_t); count > 0) {
      missing.resize(count);
      std::memcpy(missing.data(), payload.data(), payload.size());
    }

    auto const attachment = storage_->GetAttachment(file_id);
    if (!attachment || attachment->status == messaging::AttachmentStatus::discarded ||
        attachment->status == messaging::AttachmentStatus::canceled ||
        attachment->status == messaging::AttachmentStatus::peerCanceled) {
      SendCancel(peer_id, file_id);
      return;
    }
    if (attachment->status == messaging::AttachmentStatus::paused) {
      SendPause(peer_id, file_id);
      return;
    }

    auto const key = peer_id.ToString() + "_" + file_id;
    std::unique_lock<std::mutex> lock(transfers_mutex_);
    if (active_uploads_.contains(key)) {
      auto& state = active_uploads_[key];
      state.pending_chunks = missing;
      std::ranges::reverse(state.pending_chunks);

      std::size_t const num_chunks = (state.total_size + chunkSizeBytes - 1) / chunkSizeBytes;
      state.chunks_acked_absolute = num_chunks > missing.size() ? num_chunks - missing.size() : 0;
      state.chunks_sent_absolute = state.chunks_acked_absolute;

      if (!state.is_pumping && !state.paused) {
        state.is_pumping = true;
        transfer_queue_.Dispatch([this, peer_id, file_id]() -> void { PumpUploadBatch(peer_id, file_id); });
      }
    } else {
      lock.unlock();
      StartUploadInternal(peer_id, *attachment, missing);
    }
  }

  void HandleDone(ContactID const& peer_id, std::string const& file_id) {
    auto const key = peer_id.ToString() + "_" + file_id;
    {
      std::lock_guard<std::mutex> const lock(transfers_mutex_);
      active_uploads_.erase(key);
    }
    storage_->UpdateAttachmentStatus(file_id, messaging::AttachmentStatus::done, true);
    EventQueue::Instance().Push(
        EventAttachmentStatusChanged{TopicID(peer_id), file_id, messaging::AttachmentStatus::done}
    );
  }

  auto
  SendPacket(ContactID const& peer_id, std::string const& file_id, MessageType type, std::span<std::byte const> payload)
      -> bool {
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
    return transport_->SendData(peer_id, buf);
  }

  void SendMissing(ContactID const& peer_id, std::string const& file_id, std::vector<std::uint32_t> const& missing) {
    std::vector<std::byte> payload(missing.size() * sizeof(std::uint32_t));
    if (!missing.empty()) {
      std::memcpy(payload.data(), missing.data(), payload.size());
    }
    SendPacket(peer_id, file_id, MessageType::missing, payload);
  }

  void SendDone(ContactID const& peer_id, std::string const& file_id) {
    SendPacket(peer_id, file_id, MessageType::done, {});
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

  void SendAck(ContactID const& peer_id, std::string const& file_id, std::uint64_t bytes_written) {
    std::vector<std::byte> payload(sizeof(bytes_written));
    std::memcpy(payload.data(), &bytes_written, sizeof(bytes_written));
    SendPacket(peer_id, file_id, MessageType::ack, payload);
  }

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
  std::map<std::string, TransferUploadState> active_uploads_;
  std::map<std::string, TransferDownloadState> active_downloads_;
  mutable std::mutex transfers_mutex_;

  struct QueuedTransfer {
    ContactID peer_id;
    std::string attachment_id;
    std::uint64_t message_id = 0;
  };
  std::deque<QueuedTransfer> upload_queue_;
  std::deque<QueuedTransfer> download_queue_;

  DispatchQueue transfer_queue_{"FileTransfer", 1};
};

}  // namespace eillance::services
