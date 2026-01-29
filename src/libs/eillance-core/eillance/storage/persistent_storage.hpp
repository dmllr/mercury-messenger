#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::identity {

enum class ContactStatus : std::uint8_t {
  pendingApproval,   // We sent a request, waiting for them to confirm.
  awaitingApproval,  // They sent a request, we need to confirm.
  confirmed,
  rejected,
};

struct Contact {
  ContactID peer_id;
  ContactStatus status;
  std::optional<std::string> display_name;
  std::optional<std::string> display_name_override;
  std::optional<std::string> bio;
  std::optional<std::string> photo_base64;
  std::optional<std::vector<std::byte>> signature_from_them{};
  std::optional<std::vector<std::byte>> my_signature_for_them{};
};

struct ProfileInfo {
  std::string display_name;
  std::string bio;
  std::string photo_base64;
};

}  // namespace eillance::identity

namespace eillance::storage {

struct PendingAction {
  int64_t id;
  ContactID peer_id;
  std::string payload;  // JSON content
};

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

  // --- Resilience (Pending Actions) ---
  virtual void AddPendingAction(ContactID const& peer_id, std::string const& payload) = 0;
  virtual auto GetPendingActions(ContactID const& peer_id) -> std::vector<PendingAction> = 0;
  virtual void RemovePendingAction(int64_t id) = 0;

  // --- History ---
  virtual auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord = 0;
  virtual void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) = 0;
  virtual void
  UpdateMessageStatus(ContactID const& peer_id, uint64_t message_id, messaging::MessageStatus new_status) = 0;
  virtual void UpdateMessageStatuses(
      ContactID const& peer_id,
      std::vector<uint64_t> const& message_ids,
      messaging::MessageStatus new_status
  ) = 0;
  virtual void
  UpdateAttachmentStatus(std::string const& attachment_id, messaging::AttachmentStatus status, bool is_downloaded) = 0;
  virtual auto GetAttachment(std::string const& attachment_id) -> std::optional<messaging::Attachment> = 0;

  virtual auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage = 0;
  virtual auto GetFullMessageContent(ContactID const& peer_id, std::uint64_t message_id)
      -> std::optional<std::string> = 0;
  virtual auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> = 0;
  virtual auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> = 0;
  // New method to find messages containing specific attachments that are pending transfer
  virtual auto GetPendingAttachments(ContactID const& peer_id) -> std::vector<messaging::Attachment> = 0;
  virtual auto GetRemoteIds(ContactID const& peer_id, std::vector<uint64_t> const& local_ids)
      -> std::vector<uint64_t> = 0;

  // --- Debug ---
  virtual void DebugClearAllHistory() = 0;
  virtual void DebugClearAllContacts() = 0;
  virtual void DebugRevokeAllContactApprovals() = 0;
};

// Public-facing class that uses the pimpl idiom.
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

  // --- Resilience ---
  void AddPendingAction(ContactID const& peer_id, std::string const& payload) const {
    pimpl_->AddPendingAction(peer_id, payload);
  }
  [[nodiscard]] auto GetPendingActions(ContactID const& peer_id) const {
    return pimpl_->GetPendingActions(peer_id);
  }
  void RemovePendingAction(int64_t id) const {
    pimpl_->RemovePendingAction(id);
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
