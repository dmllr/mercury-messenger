#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

 #include <variant>
#include "eillance/utils/time.hpp"
#include <nlohmann/json.hpp>

namespace eillance::identity {
enum class ContactStatus {
  pendingApproval,  // We sent a request, waiting for them to confirm.
  awaitingApproval,  // They sent a request, we need to confirm.
  confirmed,
};

// Helper for JSON serialization
NLOHMANN_JSON_SERIALIZE_ENUM(  // NOLINT
    ContactStatus,
    {
        {ContactStatus::pendingApproval, "pending_approval"},
        {ContactStatus::awaitingApproval, "awaiting_approval"},
        {ContactStatus::confirmed, "confirmed"},
    })

struct Contact {
  std::string peer_id;
  ContactStatus status;
};

// Helper for JSON serialization
inline void to_json(nlohmann::json& j, const Contact& c) {
  j = nlohmann::json{{"peer_id", c.peer_id}, {"status", c.status}};
}

inline void from_json(const nlohmann::json& j, Contact& c) {
  j.at("peer_id").get_to(c.peer_id);
  j.at("status").get_to(c.status);
}
}  // namespace eillance::identity

namespace eillance::messaging {
enum class MessageDirection : std::uint8_t { sent, received };
enum class CallDirection : std::uint8_t { outgoing, incoming };
enum class CallStatus : std::uint8_t { answered, missed, rejected, busy };
enum class MessageStatus : std::uint8_t { none, sending, sent, delivered, read };
enum class HistoryEntryKind : std::uint8_t { textMessage, voiceCall, videoCall, systemMessage };

NLOHMANN_JSON_SERIALIZE_ENUM(MessageDirection, { {MessageDirection::sent, "sent"}, {MessageDirection::received, "received"}, })
NLOHMANN_JSON_SERIALIZE_ENUM(CallDirection, { {CallDirection::outgoing, "outgoing"}, {CallDirection::incoming, "incoming"}, })
NLOHMANN_JSON_SERIALIZE_ENUM(CallStatus, { {CallStatus::answered, "answered"}, {CallStatus::missed, "missed"}, {CallStatus::rejected, "rejected"}, {CallStatus::busy, "busy"}, })
NLOHMANN_JSON_SERIALIZE_ENUM(MessageStatus, { {MessageStatus::none, "none"}, {MessageStatus::sending, "sending"}, {MessageStatus::sent, "sent"}, {MessageStatus::delivered, "delivered"}, {MessageStatus::read, "read"}, })
NLOHMANN_JSON_SERIALIZE_ENUM(HistoryEntryKind, { {HistoryEntryKind::textMessage, "text_message"}, {HistoryEntryKind::voiceCall, "voice_call"}, {HistoryEntryKind::videoCall, "video_call"}, {HistoryEntryKind::systemMessage, "system_message"}, })

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
  HistoryEntryData data;

  // Transient field for passing peer_id to the database layer upon creation.
  std::string peer_id_for_db;
};

struct ChatSummary {
  std::string peer_id;
  HistoryEntryRecord last_message;
  uint32_t unread_count = 0;
};

inline void to_json(nlohmann::json& j, const HistoryEntryRecord& e) {  // NOLINT(*-identifier-naming)
  j["id"] = e.id;
  j["kind"] = e.kind;
  j["timestamp"] = e.timestamp;
  j["direction"] = e.direction;
  j["status"] = e.status;

  std::visit(
      [&j](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, MessageEntry>) {
          j["content"] = arg.content;
        } else if constexpr (std::is_same_v<T, CallEntry>) {
          j["duration_seconds"] = arg.duration_seconds;
          j["call_direction"] = arg.call_direction;
          j["call_status"] = arg.call_status;
        }
      },
      e.data);
}

inline void from_json(const nlohmann::json& j, HistoryEntryRecord& e) {  // NOLINT(*-identifier-naming)
  j.at("id").get_to(e.id);
  j.at("kind").get_to(e.kind);
  j.at("timestamp").get_to(e.timestamp);
  e.direction = j.value("direction", MessageDirection::received);
  e.status = j.value("status", MessageStatus::none);
  switch (e.kind) {
    case HistoryEntryKind::textMessage:
    case HistoryEntryKind::systemMessage:
      e.data = MessageEntry{j.at("content").get<std::string>()};
      break;
    case HistoryEntryKind::voiceCall:
    case HistoryEntryKind::videoCall:
      e.data = CallEntry{j.value("duration_seconds", 0U), j.at("call_direction").get<CallDirection>(), j.at("call_status").get<CallStatus>()};
      break;
  }
}

inline void to_json(nlohmann::json& j, const ChatSummary& s) {
  j["peer_id"] = s.peer_id;
  j["last_message"] = s.last_message;
  j["unread_count"] = s.unread_count;
}

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
  virtual void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) = 0;
  virtual void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) = 0;
  virtual auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) -> messaging::HistoryPage = 0;
  virtual auto GetChatSummaries(const std::vector<std::string>& peer_ids) -> std::vector<messaging::ChatSummary> = 0;
  virtual auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) -> std::vector<messaging::HistoryEntryRecord> = 0;
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
  void UpdateMessageStatus(const std::string& peer_id, uint64_t message_id, messaging::MessageStatus new_status) const { pimpl_->UpdateMessageStatus(peer_id, message_id, new_status); }
  void UpdateMessageStatuses(const std::string& peer_id, const std::vector<uint64_t>& message_ids, messaging::MessageStatus new_status) const { pimpl_->UpdateMessageStatuses(peer_id, message_ids, new_status); }
  auto GetHistoryPage(const std::string& peer_id, size_t page_index, size_t page_size) const { return pimpl_->GetHistoryPage(peer_id, page_index, page_size); }
  auto GetChatSummaries(const std::vector<std::string>& peer_ids) const { return pimpl_->GetChatSummaries(peer_ids); }
  auto GetMessagesWithStatus(const std::string& peer_id, messaging::MessageStatus status) const { return pimpl_->GetMessagesWithStatus(peer_id, status); }

private:
  std::unique_ptr<PersistentStorageImpl> pimpl_;
};

}  // namespace eillance::storage
