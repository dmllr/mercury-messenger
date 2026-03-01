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

enum class AttachmentStatus : std::uint8_t {
  pending = 0,
  transferring,
  done,
  failed,
  canceled,
  discarded,
  paused,
  peerCanceled
};

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
  std::string note;
  std::string file_name;  // Original filename (e.g., "report.pdf")
};

struct AttachmentRequest {
  std::string file_path;
  AttachmentType type;
  std::string thumbnail_base64;
  std::string mime_type;
  std::string note;
  std::string file_name;
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
  delivered,
  read,
};

enum class HistoryEntryKind : std::uint8_t {
  textMessage,
  voiceCall,
  videoCall,
  systemMessage,
};

enum class Reaction : std::uint8_t {
  thumbsUp = 1,
  thumbsDown = 2,
  heart = 3,
  laugh = 4,
  sad = 5,
  angry = 6,
  check = 7,
};

struct ReactionEntry {
  ContactID author_id;
  Reaction reaction;
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

struct TextEntryParams {
  ContactID peer_id;
  std::string content;
  std::vector<Attachment> attachments{};
  MessageDirection direction = MessageDirection::sent;
  HistoryEntryKind kind = HistoryEntryKind::textMessage;
  std::optional<std::uint64_t> remote_id = std::nullopt;
};

struct CallEntryParams {
  ContactID peer_id;
  HistoryEntryKind kind;
  CallDirection direction;
  CallStatus status;
  std::uint32_t duration_seconds = 0;
};

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
  std::optional<std::uint64_t> remote_id{};
  HistoryEntryKind kind{};
  Time::Unit timestamp{};
  MessageDirection direction{};
  MessageStatus status = MessageStatus::none;
  bool is_truncated = false;
  bool is_edited = false;
  bool is_deleted = false;
  HistoryEntryData data;
  ContactID peer_id;
  std::vector<ReactionEntry> reactions{};
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
