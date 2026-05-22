#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"

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
