#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/storage/persistent_storage_interface.hpp"

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

  void Close() const {
    pimpl_->Close();
  }
  auto Open() const -> bool {
    return pimpl_->Open();
  }
  void Checkpoint() const {
    pimpl_->Checkpoint();
  }

  // --- Resilience ---
  void AddPendingAction(
      ContactID const& peer_id,
      std::string const& payload,
      uint64_t timestamp = 0,
      std::optional<int64_t> ref_id = std::nullopt
  ) const {
    pimpl_->AddPendingAction(peer_id, payload, timestamp, ref_id);
  }
  [[nodiscard]] auto GetPendingActions(ContactID const& peer_id) const {
    return pimpl_->GetPendingActions(peer_id);
  }
  void RemovePendingAction(int64_t id) const {
    pimpl_->RemovePendingAction(id);
  }
  void RemovePendingActionByRefId(ContactID const& peer_id, int64_t ref_id) const {
    pimpl_->RemovePendingActionByRefId(peer_id, ref_id);
  }

  void SuspendTimedOutActions(uint64_t threshold_timestamp) const {
    pimpl_->SuspendTimedOutActions(threshold_timestamp);
  }
  void UnsuspendActionsForPeer(ContactID const& peer_id, uint64_t new_window_start) const {
    pimpl_->UnsuspendActionsForPeer(peer_id, new_window_start);
  }

  // --- History ---
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
  void UpdateAttachmentStatus(std::string const& attachment_id, messaging::AttachmentStatus status, bool is_downloaded)
      const {
    pimpl_->UpdateAttachmentStatus(attachment_id, status, is_downloaded);
  }
  [[nodiscard]] auto GetAttachment(std::string const& attachment_id) const {
    return pimpl_->GetAttachment(attachment_id);
  }
  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const {
    return pimpl_->GetHistoryPage(peer_id, page_index, page_size);
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
  [[nodiscard]] auto GetRemoteIds(ContactID const& peer_id, std::vector<uint64_t> const& local_ids) const {
    return pimpl_->GetRemoteIds(peer_id, local_ids);
  }

  // --- Debug ---
  void DebugClearAllHistory() const {
    pimpl_->DebugClearAllHistory();
  }
  void DebugClearAllContacts() const {
    pimpl_->DebugClearAllContacts();
  }
  void DebugRevokeAllContactApprovals() const {
    pimpl_->DebugRevokeAllContactApprovals();
  }

 private:
  std::unique_ptr<PersistentStorageImpl> pimpl_{};
};

}  // namespace eillance::storage
