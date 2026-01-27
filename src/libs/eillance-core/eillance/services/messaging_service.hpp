#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "eillance/messaging/delivery_service.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

struct AttachmentRequest {
  std::string file_path;
  messaging::AttachmentType type;
  std::string thumbnail_base64;
  std::string mime_type;
};

class MessagingService {
 public:
  static constexpr uint32_t currentMessageVersion = 1;

  // Flow control constants
  static constexpr size_t windowSizeChunks = 64;
  static constexpr size_t chunkSizeBytes = 16ULL * 1024;

  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  explicit MessagingService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport,
      std::shared_ptr<identity::ContactManager> contact_manager,
      ConnectionRequestHandler connection_handler
  );

  void StartDeliveryService();
  void StopDeliveryService();

  // Sending
  auto SendChatMessage(
      ContactID const& peer_id,
      std::string const& message,
      std::vector<AttachmentRequest> const& attachments = {}
  ) -> messaging::HistoryEntryRecord;

  void SendSystemMessage(ContactID const& peer_id, std::string const& content);

  // Attempts to flush queued messages and read receipts for a connected peer
  void SendQueuedMessages(ContactID const& peer_id);

  // Signal that a connection attempt finished (success or fail) to manage retry logic
  void OnConnectionAttemptFinished(ContactID const& peer_id) const;

  // File Transfer Control
  void CancelTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload);
  void ResumeTransfer(ContactID const& peer_id, std::string const& attachment_id, bool is_upload);

  // Queries
  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
      -> messaging::HistoryPage;
  [[nodiscard]] auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id) const
      -> std::optional<std::string>;
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids
  ) const -> std::vector<messaging::ChatSummary>;

  // Updates
  void MarkMessagesAsRead(ContactID const& peer_id, std::vector<std::uint64_t> const& message_ids);

  // History Ingestion (from other services)
  void AddSystemEntry(ContactID const& peer_id, std::string const& content, messaging::MessageDirection direction);
  void AddCallEntry(
      ContactID const& peer_id,
      messaging::HistoryEntryKind kind,
      messaging::CallDirection direction,
      messaging::CallStatus status,
      uint32_t duration
  );

  void ProcessIncomingFileChunk(ContactID const& peer_id, nlohmann::json const& message);

  // Incoming Events
  void OnChatMessageReceived(
      ContactID const& peer_id,
      std::vector<std::byte> const& encrypted_payload,
      uint64_t message_id,
      bool is_system,
      std::vector<messaging::Attachment> const& attachments = {}
  );
  void OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status) const;
  void OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) const;

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

  void SetChatMessageReceivedCallback(OnChatMessageCallback cb);
  void SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb);
  void SetAttachmentDownloadedCallback(OnAttachmentDownloadedCallback cb);
  void SetContactRestoredCallback(OnContactRestoredCallback cb);
  void SetTransferProgressCallback(OnTransferProgressCallback cb);
  void SetAttachmentStatusChangedCallback(OnAttachmentStatusChangedCallback cb);

 private:
  // Helper to connect to a peer if not connected
  void RequestConnection(ContactID const& peer_id);
  void UpdatePeerActivity(ContactID const& peer_id);
  // Checks DB for messages, read receipts, and contact profile sync flag
  auto HasPendingWork(ContactID const& peer_id) const -> bool;
  // Generate a unique ID for attachments
  static auto GenerateAttachmentId() -> std::string;

  void RestoreTransfers();
  void CheckForResumableUploads(ContactID const& peer_id);

  // File Transfer State Management
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

  // Starts or resumes an upload based on state
  void StartUpload(ContactID const& peer_id, messaging::Attachment const& att);
  // Sends a batch of chunks up to the window limit
  void PumpUpload(ContactID const& peer_id, std::string const& file_id);
  // Handles ACK from receiver to advance window
  void ProcessFileAck(ContactID const& peer_id, std::string const& file_id, size_t next_seq);
  // Handles Cancel message
  void ProcessFileCancel(ContactID const& peer_id, std::string const& file_id);
  // Handles Resume Request / Info
  void ProcessResumeRequest(ContactID const& peer_id, std::string const& file_id);

  static auto CalculateFileHash(std::filesystem::path const& path) -> std::string;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  ConnectionRequestHandler connection_handler_;

  std::unique_ptr<messaging::MessageHistory> history_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;

  OnChatMessageCallback on_message_received_;
  OnMessageStatusChangedCallback on_status_changed_;
  OnAttachmentDownloadedCallback on_attachment_downloaded_;
  OnContactRestoredCallback on_contact_restored_;
  OnTransferProgressCallback on_transfer_progress_;
  OnAttachmentStatusChangedCallback on_attachment_status_changed_;

  // Active File Transfers
  std::map<std::string, TransferUploadState> active_uploads_;
  std::map<std::string, TransferDownloadState> active_downloads_;
  std::mutex transfers_mutex_;
};

}  // namespace eillance::services
