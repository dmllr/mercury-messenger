#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
};

class MessagingService {
public:
  static constexpr uint32_t currentMessageVersion = 1;

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

  // Attempts to flush queued messages for a connected peer
  void SendQueuedMessages(ContactID const& peer_id);
  void SendPendingReadReceipts(ContactID const& peer_id);

  // Signal that a connection attempt finished (success or fail) to manage retry logic
  void OnConnectionAttemptFinished(ContactID const& peer_id);

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
  void OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status);
  void OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids);

  // Callbacks
  using OnChatMessageCallback = std::function<void(ContactID const& peer_id, messaging::HistoryEntryRecord const&)>;
  using OnMessageStatusChangedCallback = std::function<void(ContactID const&, uint64_t, messaging::MessageStatus)>;

  void SetChatMessageReceivedCallback(OnChatMessageCallback cb);
  void SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb);

private:
  // Helper to connect to a peer if not connected
  void RequestConnection(ContactID const& peer_id);
  void UpdatePeerActivity(ContactID const& peer_id);
  // Checks both DB and pending receipts
  auto HasPendingWork(ContactID const& peer_id) -> bool;
  // Generate a unique ID for attachments
  static auto GenerateAttachmentId() -> std::string;

  void ProcessOutgoingAttachments(ContactID const& peer_id, std::vector<messaging::Attachment> const& attachments);

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> contact_manager_;
  ConnectionRequestHandler connection_handler_;

  std::unique_ptr<messaging::MessageHistory> history_;
  std::unique_ptr<messaging::MessageDeliveryService> delivery_service_;

  OnChatMessageCallback on_message_received_;
  OnMessageStatusChangedCallback on_status_changed_;

  // Queue for read receipts that need to be sent reliably.
  // Stores REMOTE IDs (the sender's original message_id).
  std::unordered_map<ContactID, std::vector<uint64_t>> pending_read_receipts_;
  std::recursive_mutex receipts_mutex_;
};

}  // namespace eillance::services
