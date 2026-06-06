#pragma once

#include <cstdint>
#include <string>

namespace mercury::bot {

// Represents a Mercury user (peer).
struct User {
  std::string id;          // Hex string of the ContactID
  std::string first_name;  // Mapped from Display Name
  std::string bio;
};

// Represents a text message.
struct Message {
  std::uint64_t message_id{};
  User from;
  std::string chat_id;
  std::string text;
  std::uint64_t date{};  // Unix timestamp (microseconds)
};

}  // namespace mercury::bot
