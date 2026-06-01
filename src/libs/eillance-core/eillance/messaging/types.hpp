#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/utils/time.hpp"
#include "eillance/utils/timestamp.hpp"

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

struct AttachmentRecord {
  Attachment attachment;
  std::uint64_t timestamp;
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
  TopicID topic_id;
  std::string content;
  std::vector<Attachment> attachments{};
  MessageDirection direction = MessageDirection::sent;
  HistoryEntryKind kind = HistoryEntryKind::textMessage;
  std::uint64_t id = 0;
  std::uint64_t reply_to_id = 0;
  std::optional<Time::Unit> timestamp = std::nullopt;
};

struct CallEntryParams {
  TopicID topic_id;
  HistoryEntryKind kind;
  CallDirection direction;
  CallStatus status;
  std::uint32_t duration_seconds = 0;
  std::uint64_t id = 0;
  std::optional<Time::Unit> timestamp = std::nullopt;
};

struct MessageContent {
  std::string content;
  std::vector<Attachment> attachments{};
  std::uint64_t reply_to_id = 0;
};

struct PeerMessageStatus {
  ContactID peer_id;
  MessageStatus status = MessageStatus::none;
};

struct ChatMessageEntry {
  MessageContent message;
};

struct GroupMessageEntry {
  MessageContent message;
  std::vector<PeerMessageStatus> peer_statuses{};
};

struct ChannelMessageEntry {
  MessageContent message;
};

struct CallEntry {
  uint32_t duration_seconds = 0;
  CallDirection call_direction;
  CallStatus call_status;
};

using HistoryEntryData = std::variant<ChatMessageEntry, GroupMessageEntry, ChannelMessageEntry, CallEntry>;

inline auto GetMessageContent(HistoryEntryData& data) -> MessageContent* {
  if (auto* c = std::get_if<ChatMessageEntry>(&data)) return &c->message;
  if (auto* g = std::get_if<GroupMessageEntry>(&data)) return &g->message;
  if (auto* ch = std::get_if<ChannelMessageEntry>(&data)) return &ch->message;
  return nullptr;
}

inline auto GetMessageContent(HistoryEntryData const& data) -> MessageContent const* {
  if (auto const* c = std::get_if<ChatMessageEntry>(&data)) return &c->message;
  if (auto const* g = std::get_if<GroupMessageEntry>(&data)) return &g->message;
  if (auto const* ch = std::get_if<ChannelMessageEntry>(&data)) return &ch->message;
  return nullptr;
}

struct HistoryEntryRecord {
  std::uint64_t id{};
  HistoryEntryKind kind{};
  Time::Unit timestamp{};
  MessageDirection direction{};
  MessageStatus status = MessageStatus::none;
  bool is_truncated = false;
  bool is_edited = false;
  bool is_deleted = false;
  HistoryEntryData data;
  TopicID topic_id;
  std::vector<ReactionEntry> reactions{};
};

struct ChatSummary {
  TopicID topic_id;
  HistoryEntryRecord last_message;
  uint32_t unread_count = 0;
};

struct HistoryPage {
  std::vector<HistoryEntryRecord> messages{};
  size_t total_messages{};
};

enum class LedgerActionType : uint8_t {
  Message = 1,
  MessageEdit = 2,
  MessageDelete = 3,
  Reaction = 4,
  CallLog = 5,
  ProfileUpdate = 6,
  ContactState = 7,
  GroupState = 8,
  ChannelState = 9
};

struct LedgerHash {
  static constexpr auto hashSize = 32U;
  std::array<std::byte, hashSize> value{};

  constexpr LedgerHash() = default;

  explicit LedgerHash(std::span<std::byte const> span) {
    if (!span.empty()) {
      if (span.size() != hashSize) throw std::invalid_argument("LedgerHash must be 32 bytes");
      std::ranges::copy(span, value.begin());
    }
  }

  explicit LedgerHash(std::string_view hex) {
    if (!hex.empty()) {
      if (hex.size() != 2 * hashSize)
        throw std::invalid_argument("Invalid hex length for LedgerHash: " + std::string(hex));
      for (size_t i = 0; i < hashSize; ++i) {
        auto const high = HexCharToInt(hex[2 * i]);
        auto const low = HexCharToInt(hex[2 * i + 1]);
        value[i] = static_cast<std::byte>((high << 4U) | low);
      }
    }
  }

  [[nodiscard]] auto ToString() const -> std::string {
    if (IsEmpty()) return std::string(2 * hashSize, '0');
    std::string hex(2 * hashSize, '0');
    static constexpr std::array<char, 16> hexmap =
        {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (size_t i = 0; i < hashSize; ++i) {
      hex[2U * i] = hexmap[(static_cast<uint8_t>(value[i]) & 0xF0U) >> 4U];
      hex[(2U * i) + 1] = hexmap[static_cast<uint8_t>(value[i]) & 0x0FU];
    }
    return hex;
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return std::ranges::all_of(value, [](std::byte byte) { return byte == std::byte{0}; });
  }

  auto operator<=>(LedgerHash const&) const = default;
  auto operator==(LedgerHash const&) const -> bool = default;

 private:
  static constexpr auto HexCharToInt(char const chr) -> uint8_t {
    auto const c = static_cast<unsigned char>(chr);
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    throw std::invalid_argument(std::string("Invalid hex char: ") + chr);
  }
};

inline void to_json(nlohmann::json& json, LedgerHash const& hash) {
  json = hash.ToString();
}

inline void from_json(nlohmann::json const& json, LedgerHash& hash) {
  auto const strval = json.get<std::string>();
  if (strval.empty()) {
    hash = LedgerHash();
  } else {
    hash = LedgerHash(strval);
  }
}

struct LedgerEntry {
  uint64_t seq_id = 0;
  TopicID topic_id;
  ContactID author_id;
  Timestamp created_at;
  LedgerActionType type;
  std::string payload;
  LedgerHash prev_hash;
  LedgerHash hash;
};

struct SyncRequest {
  TopicID topic_id;
  ContactID target_peer_id;
  Timestamp created_at;
  Timestamp last_attempt;
};

}  // namespace eillance::messaging
