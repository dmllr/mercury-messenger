#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eillance/utils/time.hpp"
#include <nlohmann/json.hpp>
#include <variant>

namespace eillance::identity {

enum class ContactStatus : std::uint8_t {
  pendingApproval,   // We sent a request, waiting for them to confirm.
  awaitingApproval,  // They sent a request, we need to confirm.
  confirmed,
};

struct Contact {
  std::string peer_id;
  ContactStatus status;
  std::optional<std::string> display_name;
  std::optional<std::string> display_name_override;
  std::optional<std::string> bio;
  std::optional<std::string> photo_base64;
  std::optional<std::string> signature_from_them;  // hex
  std::optional<std::string> my_signature_for_them;  // hex
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

NLOHMANN_JSON_SERIALIZE_ENUM(MessageDirection, {
  {MessageDirection::sent, "sent"},
  {MessageDirection::received, "received"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(CallDirection, {
  {CallDirection::outgoing, "outgoing"},
  {CallDirection::incoming, "incoming"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(CallStatus, {
  {CallStatus::answered, "answered"},
  {CallStatus::missed, "missed"},
  {CallStatus::rejected, "rejected"},
  {CallStatus::busy, "busy"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(MessageStatus, {
  {MessageStatus::none, "none"},
  {MessageStatus::sending, "sending"},
  {MessageStatus::sent, "sent"},
  {MessageStatus::delivered, "delivered"},
  {MessageStatus::read, "read"},
})
NLOHMANN_JSON_SERIALIZE_ENUM(HistoryEntryKind, {
  {HistoryEntryKind::textMessage, "text_message"},
  {HistoryEntryKind::voiceCall, "voice_call"},
  {HistoryEntryKind::videoCall, "video_call"},
  {HistoryEntryKind::systemMessage, "system_message"},
})

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
  std::uint64_t id;
  HistoryEntryKind kind;
  Time::Unit timestamp;
  MessageDirection direction;
  MessageStatus status = MessageStatus::none;
  bool is_truncated = false;
  HistoryEntryData data;

  // Transient field for passing peer_id to the database layer upon creation.
  std::string peer_id_for_db;
};

struct ChatSummary {
  std::string peer_id;
  HistoryEntryRecord last_message;
  uint32_t unread_count = 0;
};

struct HistoryPage {
  std::vector<HistoryEntryRecord> messages;
  size_t total_messages;
};

}  // namespace eillance::messaging

namespace eillance::storage {

// The abstract interface for platform-specific storage implementations.
// Core depends on this, but implementations will live in platform-specific projects.
class PersistentStorageImpl {
public:
  virtual ~PersistentStorageImpl() = default;

  virtual auto GetPath() const -> std::string = 0;
  // --- Settings ---
  virtual auto GetSetting(std::string_view key) -> std::optional<std::string> = 0;
  virtual void SetSetting(std::string_view key, std::string_view value) = 0;
  virtual void DeleteSetting(std::string_view key) = 0;
  virtual auto GetRaw(const std::string& key) -> std::optional<std::vector<std::byte>> = 0;
  virtual auto SetRaw(const std::string& key, const std::vector<std::byte>& value) -> bool = 0;

  // --- Contacts ---
  virtual auto GetContacts() -> std::vector<identity::Contact> = 0;
  virtual auto GetContact(std::string_view peer_id) -> std::optional<identity::Contact> = 0;
  virtual void UpdateContact(const identity::Contact& contact) = 0;
  virtual void RemoveContact(std::string_view peer_id) = 0;

  // --- History ---
  virtual auto AddHistoryEntry(messaging::HistoryEntryRecord entry) -> messaging::HistoryEntryRecord = 0;
  virtual void UpdateHistoryEntry(const messaging::HistoryEntryRecord& entry) = 0;
  virtual void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) = 0;
  virtual void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) = 0;
  virtual auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) -> messaging::HistoryPage = 0;
  virtual auto GetFullMessageContent(std::uint64_t message_id) -> std::optional<std::string> = 0;
  virtual auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<messaging::ChatSummary> = 0;
  virtual auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) -> std::vector<messaging::HistoryEntryRecord> = 0;

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
  explicit PersistentStorage(std::unique_ptr<PersistentStorageImpl> pimpl) : pimpl_(std::move(pimpl)) {}

  ~PersistentStorage() = default;
  PersistentStorage(const PersistentStorage&) = delete;
  auto operator=(const PersistentStorage&) -> PersistentStorage& = delete;
  PersistentStorage(PersistentStorage&&) noexcept = default;
  auto operator=(PersistentStorage&&) noexcept -> PersistentStorage& = default;

  auto GetPath() const { return pimpl_->GetPath(); }
  // --- Settings ---
  auto GetSetting(std::string_view key) const { return pimpl_->GetSetting(key); }
  void SetSetting(std::string_view key, std::string_view value) const { pimpl_->SetSetting(key, value); }
  void DeleteSetting(std::string_view key) const { pimpl_->DeleteSetting(key); }
  auto GetRaw(const std::string& key) const { return pimpl_->GetRaw(key); }
  auto SetRaw(const std::string& key, const std::vector<std::byte>& value) const { return pimpl_->SetRaw(key, value); }

  // --- Contacts ---
  auto GetContacts() const { return pimpl_->GetContacts(); }
  auto GetContact(std::string_view peer_id) const { return pimpl_->GetContact(peer_id); }
  void UpdateContact(const identity::Contact& contact) const { pimpl_->UpdateContact(contact); }
  void RemoveContact(std::string_view peer_id) const { pimpl_->RemoveContact(peer_id); }

  // --- History ---
  auto AddHistoryEntry(messaging::HistoryEntryRecord entry) const { return pimpl_->AddHistoryEntry(std::move(entry)); }
  void UpdateHistoryEntry(const messaging::HistoryEntryRecord& entry) const { pimpl_->UpdateHistoryEntry(entry); }
  void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) const { pimpl_->UpdateMessageStatus(peer_id, message_id, new_status); }
  void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) const { pimpl_->UpdateMessageStatuses(peer_id, message_ids, new_status); }
  auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const { return pimpl_->GetHistoryPage(peer_id, page_index, page_size); }
  auto GetFullMessageContent(std::uint64_t message_id) const { return pimpl_->GetFullMessageContent(message_id); }
  auto GetChatSummaries(const std::vector<std::string>& peer_ids) const { return pimpl_->GetChatSummaries(peer_ids); }
  auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) const { return pimpl_->GetMessagesWithStatus(peer_id, status); }

  // --- Debug ---
  void DebugClearAllHistory() const { pimpl_->DebugClearAllHistory(); }
  void DebugClearAllContacts() const { pimpl_->DebugClearAllContacts(); }
  void DebugRevokeAllContactApprovals() const { pimpl_->DebugRevokeAllContactApprovals(); }

private:
  std::unique_ptr<PersistentStorageImpl> pimpl_;
};

}  // namespace eillance::storage
