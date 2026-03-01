#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"

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

struct PendingAction {
  int64_t id;
  ContactID peer_id;
  std::string payload;            // JSON content
  uint64_t created_at;            // Timestamp (microseconds)
  std::optional<int64_t> ref_id;  // Reference ID (e.g. message_id), optional
  bool is_suspended = false;
  uint64_t window_start_at = 0;
};

struct PendingActionParams {
  ContactID peer_id;
  std::string payload;
  uint64_t timestamp = 0;
  std::optional<int64_t> ref_id = std::nullopt;
};

}  // namespace eillance::storage
