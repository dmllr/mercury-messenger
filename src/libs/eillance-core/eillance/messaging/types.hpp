#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::messaging {

enum class AttachmentType : std::uint8_t { image, video, file };

NLOHMANN_JSON_SERIALIZE_ENUM(
    AttachmentType,
    {
        {AttachmentType::image, "image"},
        {AttachmentType::video, "video"},
        {AttachmentType::file, "file"},
    }
)

enum class AttachmentStatus : std::uint8_t { pending = 0, transferring, done, failed, canceled, discarded };

struct Attachment {
  std::string id;
  AttachmentType type;
  std::string file_path;
  std::string mime_type;
  std::uint64_t size_bytes;
  std::string thumbnail_base64;
  std::string hash;  // SHA-256 hash of the file content
  bool is_downloaded = false;
  AttachmentStatus status = AttachmentStatus::pending;
};

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

struct MessageEntry {
  std::string content;
  std::vector<Attachment> attachments{};
};

struct CallEntry {
  uint32_t duration_seconds = 0;
  CallDirection call_direction;
  CallStatus call_status;
};

using HistoryEntryData = std::variant<MessageEntry, CallEntry>;

struct HistoryEntryRecord {
  std::uint64_t id{};
  std::optional<std::uint64_t> remote_id;
  HistoryEntryKind kind;
  Time::Unit timestamp{};
  MessageDirection direction;
  MessageStatus status = MessageStatus::none;
  bool is_truncated = false;
  HistoryEntryData data;
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
