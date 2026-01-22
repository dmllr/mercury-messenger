#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"
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

namespace eillance::messaging {

enum class MessageDirection : std::uint8_t {
  sent,
  received,
};

enum class CallDirection : std::uint8_t {
  outgoing,
  incoming,
};

enum class CallStatus : std::uint8_t {
  answered,
  missed,
  rejected,
  busy,
};

enum class MessageStatus : std::uint8_t {
  none,
  sending,
  sent,
  delivered,
  read,
};

enum class HistoryEntryKind : std::uint8_t {
  textMessage,
  voiceCall,
  videoCall,
  systemMessage,
};

NLOHMANN_JSON_SERIALIZE_ENUM(
    MessageDirection,
    {
        {MessageDirection::sent, "sent"},
        {MessageDirection::received, "received"},
    }
)
NLOHMANN_JSON_SERIALIZE_ENUM(
    CallDirection,
    {
        {CallDirection::outgoing, "outgoing"},
        {CallDirection::incoming, "incoming"},
    }
)
NLOHMANN_JSON_SERIALIZE_ENUM(
    CallStatus,
    {
        {CallStatus::answered, "answered"},
        {CallStatus::missed, "missed"},
        {CallStatus::rejected, "rejected"},
        {CallStatus::busy, "busy"},
    }
)
NLOHMANN_JSON_SERIALIZE_ENUM(
    MessageStatus,
    {
        {MessageStatus::none, "none"},
        {MessageStatus::sending, "sending"},
        {MessageStatus::sent, "sent"},
        {MessageStatus::delivered, "delivered"},
        {MessageStatus::read, "read"},
    }
)
NLOHMANN_JSON_SERIALIZE_ENUM(
    HistoryEntryKind,
    {
        {HistoryEntryKind::textMessage, "text_message"},
        {HistoryEntryKind::voiceCall, "voice_call"},
        {HistoryEntryKind::videoCall, "video_call"},
        {HistoryEntryKind::systemMessage, "system_message"},
    }
)

// Data specific to a text or system message.
struct MessageEntry {
  std::string content;
};

// Data specific to a voice or video call record.
struct CallEntry {
  uint32_t duration_seconds = 0;
  CallDirection call_direction;
  CallStatus call_status;
};

// A variant to hold the data for any kind of history entry.
using HistoryEntryData = std::variant<MessageEntry, CallEntry>;

struct HistoryEntryRecord {
  std::uint64_t id{};
  HistoryEntryKind kind;
  Time::Unit timestamp{};
  MessageDirection direction;
  MessageStatus status = MessageStatus::none;
  bool is_truncated = false;
  HistoryEntryData data;

  // Peer ID associated with this record
  ContactID peer_id;
};

struct ChatSummary {
  ContactID peer_id;
  HistoryEntryRecord last_message;
  uint32_t unread_count = 0;
};

struct HistoryPage {
  std::vector<HistoryEntryRecord> messages{};
  size_t total_messages{};
};

}  // namespace eillance::messaging

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
  virtual auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size)
      -> messaging::HistoryPage = 0;
  virtual auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> = 0;
  virtual auto GetChatSummaries(std::vector<ContactID> const& peer_ids) -> std::vector<messaging::ChatSummary> = 0;
  virtual auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status)
      -> std::vector<messaging::HistoryEntryRecord> = 0;

  // --- Debug ---
  virtual void DebugClearAllHistory() = 0;
  virtual void DebugClearAllContacts() = 0;
  virtual void DebugRevokeAllContactApprovals() = 0;
};

// Public-facing class that uses the pimpl idiom.
// This wrapper is instantiated in platform-specific code (which provides the impl)
// and then passed to core components that require storage access.
class PersistentStorage {
 public:
  // The constructor now accepts a pre-constructed, platform-specific implementation.
  explicit PersistentStorage(std::unique_ptr<PersistentStorageImpl> pimpl)
      : pimpl_(std::move(pimpl)) {}

  ~PersistentStorage() = default;
  PersistentStorage(PersistentStorage const&) = delete;
  auto operator=(PersistentStorage const&) -> PersistentStorage& = delete;
  PersistentStorage(PersistentStorage&&) noexcept = default;
  auto operator=(PersistentStorage&&) noexcept -> PersistentStorage& = default;

  [[nodiscard]] auto GetPath() const { return pimpl_->GetPath(); }
  // --- Settings ---
  [[nodiscard]] auto GetSetting(std::string_view key) const { return pimpl_->GetSetting(key); }
  void SetSetting(std::string_view key, std::string_view value) const { pimpl_->SetSetting(key, value); }
  void DeleteSetting(std::string_view key) const { pimpl_->DeleteSetting(key); }
  [[nodiscard]] auto GetRaw(std::string const& key) const { return pimpl_->GetRaw(key); }
  [[nodiscard]] auto SetRaw(std::string const& key, std::vector<std::byte> const& value) const {
    return pimpl_->SetRaw(key, value);
  }

  // --- Contacts ---
  [[nodiscard]] auto GetContacts() const { return pimpl_->GetContacts(); }
  [[nodiscard]] auto GetContact(ContactID const& peer_id) const { return pimpl_->GetContact(peer_id); }
  void UpdateContact(identity::Contact const& contact) const { pimpl_->UpdateContact(contact); }
  void RemoveContact(ContactID const& peer_id) const { pimpl_->RemoveContact(peer_id); }

  // --- History ---
  [[nodiscard]] auto AddHistoryEntry(messaging::HistoryEntryRecord entry) const {
    return pimpl_->AddHistoryEntry(std::move(entry));
  }
  void UpdateHistoryEntry(messaging::HistoryEntryRecord const& entry) const { pimpl_->UpdateHistoryEntry(entry); }
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
  [[nodiscard]] auto GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const {
    return pimpl_->GetHistoryPage(peer_id, page_index, page_size);
  }
  [[nodiscard]] auto GetFullMessageContent(std::uint64_t message_id) const {
    return pimpl_->GetFullMessageContent(message_id);
  }
  [[nodiscard]] auto GetChatSummaries(std::vector<ContactID> const& peer_ids) const {
    return pimpl_->GetChatSummaries(peer_ids);
  }
  [[nodiscard]] auto GetMessagesWithStatus(ContactID const& peer_id, messaging::MessageStatus status) const {
    return pimpl_->GetMessagesWithStatus(peer_id, status);
  }

  // --- Debug ---
  void DebugClearAllHistory() const { pimpl_->DebugClearAllHistory(); }
  void DebugClearAllContacts() const { pimpl_->DebugClearAllContacts(); }
  void DebugRevokeAllContactApprovals() const { pimpl_->DebugRevokeAllContactApprovals(); }

 private:
  std::unique_ptr<PersistentStorageImpl> pimpl_{};
};

}  // namespace eillance::storage
