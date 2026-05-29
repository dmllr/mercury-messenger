#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "eillance/identity/contact.hpp"
#include "eillance/identity/group.hpp"
#include "eillance/messaging/types.hpp"

namespace eillance::storage {

// The abstract interface for platform-specific storage implementations.
// Core depends on this, but implementations will live in platform-specific projects.
class PersistentStorageImpl {
 public:
  virtual ~PersistentStorageImpl() = default;

  [[nodiscard]] virtual auto GetPath() const -> std::string = 0;

  // --- Settings ---
  virtual auto GetSetting(std::string_view key) -> std::optional<std::string> = 0;
  virtual void SetSetting(std::string_view key, std::string_view value) = 0;
  virtual void DeleteSetting(std::string_view key) = 0;
  virtual auto GetRaw(std::string const& key) -> std::optional<std::vector<std::byte>> = 0;
  virtual auto SetRaw(std::string const& key, std::vector<std::byte> const& value) -> bool = 0;

  // --- Contacts ---
  virtual auto GetContacts() -> std::vector<identity::Contact> = 0;
  virtual auto GetContact(ContactID const& peer_id) -> std::optional<identity::Contact> = 0;
  virtual void UpdateContact(identity::Contact const& contact) = 0;
  virtual void RemoveContact(ContactID const& peer_id) = 0;

  // --- Groups ---
  virtual void AddGroup(identity::Group const& group) = 0;
  virtual auto GetGroup(GroupID const& group_id) -> std::optional<identity::Group> = 0;
  virtual auto GetGroups() -> std::vector<identity::Group> = 0;
  virtual void RemoveGroup(GroupID const& group_id) = 0;

  // --- Lifecycle ---
  virtual void Close() = 0;
  virtual auto Open() -> bool = 0;
  virtual void Checkpoint() = 0;

  // --- Synchronization Ledger ---
  virtual void AddLedgerEntry(messaging::LedgerEntry const& entry) = 0;
  virtual auto GetLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::vector<messaging::LedgerEntry> = 0;
  virtual auto GetLedgerEntries(TopicID const& topic_id) -> std::vector<messaging::LedgerEntry> = 0;
  virtual auto GetLatestLedgerEntry(TopicID const& topic_id) -> std::optional<messaging::LedgerEntry> = 0;
  virtual auto GetLedgerEntryByHash(TopicID const& topic_id, messaging::LedgerHash const& hash)
      -> std::optional<messaging::LedgerEntry> = 0;
  virtual void DeleteLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash) = 0;
  virtual auto GetRecentLedgerHashes(TopicID const& topic_id) -> std::vector<messaging::LedgerHash> = 0;
  virtual auto HasLedgerEntry(TopicID const& topic_id, messaging::LedgerHash const& hash) -> bool = 0;
  virtual void ReplaceLedgerEntries(
      TopicID const& topic_id,
      messaging::LedgerHash const& lca_hash,
      std::vector<messaging::LedgerEntry> const& entries
  ) = 0;

  // --- Sync Requests ---
  virtual void AddSyncRequest(messaging::SyncRequest const& req) = 0;
  virtual auto GetSyncRequests() -> std::vector<messaging::SyncRequest> = 0;
  virtual void RemoveSyncRequest(TopicID const& topic_id, ContactID const& target_peer_id) = 0;
  virtual void
  UpdateSyncRequestAttempt(TopicID const& topic_id, ContactID const& target_peer_id, Timestamp attempt_time) = 0;

  // --- History ---
  virtual void ClearTopicReadModel(TopicID const& topic_id) = 0;
  virtual auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord = 0;
  virtual void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) = 0;
  virtual void
  UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status) = 0;
  virtual void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) = 0;
  virtual void UpdateMessageContent(ContactID const& peer_id, uint64_t message_id, std::string const& new_content) = 0;
  virtual void MarkMessageDeleted(ContactID const& peer_id, uint64_t message_id) = 0;
  virtual void ToggleReaction(
      ContactID const& peer_id,
      uint64_t message_id,
      ContactID const& author_id,
      messaging::Reaction reaction
  ) = 0;

  virtual void
  UpdateAttachmentStatus(std::string const& attachment_id, messaging::AttachmentStatus status, bool is_downloaded) = 0;
  virtual auto GetAttachment(std::string const& attachment_id) -> std::optional<messaging::Attachment> = 0;
  virtual auto GetDownloadedAttachments(ContactID const& peer_id) -> std::vector<messaging::AttachmentRecord> = 0;
  virtual auto GetAllAttachments(ContactID const& peer_id) -> std::vector<messaging::Attachment> = 0;

  virtual auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage = 0;
  virtual auto GetHistoryEntry(ContactID const& peer_id, uint64_t message_id)
      -> std::optional<messaging::HistoryEntryRecord> = 0;
  virtual auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id)
      -> std::optional<std::string> = 0;
  virtual auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> = 0;
  virtual auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> = 0;
  virtual auto GetPendingAttachments(ContactID const& peer_id) -> std::vector<messaging::Attachment> = 0;

  // --- Search ---
  virtual auto SearchMessages(ContactID const& peer_id, std::string const& query)
      -> std::vector<messaging::HistoryEntryRecord> = 0;
  virtual auto SearchAllMessages(std::string const& query) -> std::vector<messaging::HistoryEntryRecord> = 0;
};

}  // namespace eillance::storage
