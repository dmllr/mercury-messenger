#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/utils/timestamp.hpp"

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

struct ProfileUpdate {
  std::optional<std::string> display_name;
  std::optional<std::string> bio;
  std::optional<std::string> photo_base64;
};

}  // namespace eillance::identity

namespace eillance::storage {

enum class LedgerActionType : uint8_t {
  Message = 1,
  MessageEdit = 2,
  MessageDelete = 3,
  Reaction = 4,
  CallLog = 5,
  ProfileUpdate = 6,
  ContactState = 7
};

struct LedgerEntry {
  uint64_t seq_id = 0;
  TopicID topic_id;
  ContactID author_id;
  Timestamp created_at;
  LedgerActionType type;
  std::string payload;
  std::string prev_hash;
  std::string hash;
};

struct SyncRequest {
  TopicID topic_id;
  ContactID target_peer_id;
  Timestamp created_at;
  Timestamp last_attempt;
};

}  // namespace eillance::storage
