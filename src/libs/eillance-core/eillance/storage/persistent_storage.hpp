#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/storage/persistent_storage_interface.hpp"
#include "eillance/identity/group.hpp"

namespace eillance::storage {

class PersistentStorage {
 public:
  explicit PersistentStorage(std::unique_ptr<PersistentStorageImpl> pimpl)
      : pimpl_(std::move(pimpl)) {
  }

  ~PersistentStorage() = default;
  PersistentStorage(PersistentStorage const&) = delete;
  auto operator=(PersistentStorage const&) -> PersistentStorage& = delete;
  PersistentStorage(PersistentStorage&&) noexcept = default;
  auto operator=(PersistentStorage&&) noexcept -> PersistentStorage& = default;

  [[nodiscard]] auto GetPath() const {
    return pimpl_->GetPath();
  }
  // --- Settings ---
  [[nodiscard]] auto GetSetting(std::string_view key) const {
    return pimpl_->GetSetting(key);
  }
  void SetSetting(std::string_view key, std::string_view value) const {
    pimpl_->SetSetting(key, value);
  }
  void DeleteSetting(std::string_view key) const {
    pimpl_->DeleteSetting(key);
  }
  [[nodiscard]] auto GetRaw(std::string const& key) const {
    return pimpl_->GetRaw(key);
  }
  [[nodiscard]] auto SetRaw(std::string const& key, std::vector<std::byte> const& value) const {
    return pimpl_->SetRaw(key, value);
  }

  // --- Contacts ---
  [[nodiscard]] auto GetContacts() const {
    return pimpl_->GetContacts();
  }
  [[nodiscard]] auto GetContact(ContactID const& peer_id) const {
    return pimpl_->GetContact(peer_id);
  }
  void UpdateContact(identity::Contact const& contact) const {
    pimpl_->UpdateContact(contact);
  }
  void RemoveContact(ContactID const& peer_id) const {
    pimpl_->RemoveContact(peer_id);
  }

  // --- Groups ---
  void AddGroup(identity::Group const& group) const { pimpl_->AddGroup(group); }
  [[nodiscard]] auto GetGroup(GroupID const& group_id) const { return pimpl_->GetGroup(group_id); }
  [[nodiscard]] auto GetGroups() const { return pimpl_->GetGroups(); }
  void RemoveGroup(GroupID const& group_id) const { pimpl_->RemoveGroup(group_id); }

  void Close() const {
    pimpl_->Close();
  }
  auto Open() const -> bool {
    return pimpl_->Open();
  }
  void Checkpoint() const {
    pimpl_->Checkpoint();
  }

  // --- Synchronization Ledger ---
  void AddLedgerEntry(messaging::LedgerEntry const& entry) const {
    pimpl_->AddLedgerEntry(entry);
  }
  [[nodiscard]] auto GetLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash) const {
    return pimpl_->GetLedgerEntriesAfter(topic_id, hash);
  }
  [[nodiscard]] auto GetLedgerEntries(TopicID const& topic_id) const {
    return pimpl_->GetLedgerEntries(topic_id);
  }
  [[nodiscard]] auto GetLatestLedgerEntry(TopicID const& topic_id) const {
    return pimpl_->GetLatestLedgerEntry(topic_id);
  }
  [[nodiscard]] auto GetLedgerEntryByHash(TopicID const& topic_id, messaging::LedgerHash const& hash) const {
    return pimpl_->GetLedgerEntryByHash(topic_id, hash);
  }
  void DeleteLedgerEntriesAfter(TopicID const& topic_id, messaging::LedgerHash const& hash) const {
    pimpl_->DeleteLedgerEntriesAfter(topic_id, hash);
  }
  [[nodiscard]] auto GetRecentLedgerHashes(TopicID const& topic_id) const {
    return pimpl_->GetRecentLedgerHashes(topic_id);
  }
  [[nodiscard]] auto HasLedgerEntry(TopicID const& topic_id, messaging::LedgerHash const& hash) const {
    return pimpl_->HasLedgerEntry(topic_id, hash);
  }
  void ReplaceLedgerEntries(
      TopicID const& topic_id,
      messaging::LedgerHash const& lca_hash,
      std::vector<messaging::LedgerEntry> const& entries
  ) const {
    pimpl_->ReplaceLedgerEntries(topic_id, lca_hash, entries);
  }

  // --- Sync Requests ---
  void AddSyncRequest(messaging::SyncRequest const& req) const {
    pimpl_->AddSyncRequest(req);
  }
  [[nodiscard]] auto GetSyncRequests() const {
    return pimpl_->GetSyncRequests();
  }
  void RemoveSyncRequest(TopicID const& topic_id, ContactID const& target_peer_id) const {
    pimpl_->RemoveSyncRequest(topic_id, target_peer_id);
  }
  void
  UpdateSyncRequestAttempt(TopicID const& topic_id, ContactID const& target_peer_id, Timestamp attempt_time) const {
    pimpl_->UpdateSyncRequestAttempt(topic_id, target_peer_id, attempt_time);
  }

  // --- History ---
  void ClearTopicReadModel(TopicID const& topic_id) const {
    pimpl_->ClearTopicReadModel(topic_id);
  }
  [[nodiscard]] auto AddHistoryEntry(messaging::HistoryEntryRecord entry) {
    return pimpl_->AddHistoryEntry(std::move(entry));
  }
  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) const {
    pimpl_->UpdateHistoryEntry(entry);
  }
  void UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status) const {
    pimpl_->UpdateMessageStatus(peer_id, message_id, new_status);
  }
  void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) const {
    pimpl_->UpdateMessageStatuses(peer_id, message_ids, new_status);
  }
  virtual void UpdateMessageContent(ContactID const& peer_id, uint64_t message_id, std::string const& new_content) {
    pimpl_->UpdateMessageContent(peer_id, message_id, new_content);
  };
  virtual void MarkMessageDeleted(ContactID const& peer_id, uint64_t message_id) {
    pimpl_->MarkMessageDeleted(peer_id, message_id);
  }
  virtual void ToggleReaction(
      ContactID const& peer_id,
      uint64_t message_id,
      ContactID const& author_id,
      messaging::Reaction reaction
  ) {
    pimpl_->ToggleReaction(peer_id, message_id, author_id, reaction);
  }
  void UpdateAttachmentStatus(
      std::string const& attachment_id,
      messaging::AttachmentStatus status,
      bool is_downloaded
  ) const {
    pimpl_->UpdateAttachmentStatus(attachment_id, status, is_downloaded);
  }
  [[nodiscard]] auto GetAttachment(std::string const& attachment_id) const {
    return pimpl_->GetAttachment(attachment_id);
  }
  [[nodiscard]] auto GetDownloadedAttachments(ContactID const& peer_id) const {
    return pimpl_->GetDownloadedAttachments(peer_id);
  }
  [[nodiscard]] auto GetAllAttachments(ContactID const& peer_id) const {
    return pimpl_->GetAllAttachments(peer_id);
  }
  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const {
    return pimpl_->GetHistoryPage(peer_id, page_index, page_size);
  }
  [[nodiscard]] auto GetHistoryEntry(ContactID const& peer_id, uint64_t const message_id) const
      -> std::optional<messaging::HistoryEntryRecord> {
    return pimpl_->GetHistoryEntry(peer_id, message_id);
  }

  [[nodiscard]] auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id) const {
    return pimpl_->GetFullMessageContent(peer_id, message_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids) const {
    return pimpl_->GetChatSummaries(peer_ids);
  }
  [[nodiscard]] auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status) const {
    return pimpl_->GetMessagesWithStatus(peer_id, status);
  }
  [[nodiscard]] auto GetPendingAttachments(ContactID const& peer_id) const {
    return pimpl_->GetPendingAttachments(peer_id);
  }

  // --- Search ---
  [[nodiscard]] auto SearchMessages(ContactID const& peer_id, std::string const& query) const {
    return pimpl_->SearchMessages(peer_id, query);
  }
  [[nodiscard]] auto SearchAllMessages(std::string const& query) const {
    return pimpl_->SearchAllMessages(query);
  }

 private:
  std::unique_ptr<PersistentStorageImpl> pimpl_{};
};

}  // namespace eillance::storage
