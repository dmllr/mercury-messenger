#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace eillance {

struct ContactID {
  static constexpr auto identitySize = 32U;

  constexpr ContactID() = default;

  explicit ContactID(const std::array<uint8_t, identitySize>& value) : value(value) {}

  // Construct from raw bytes
  explicit ContactID(const std::vector<std::byte>& vec) {
    if (vec.size() != identitySize) {
      throw std::invalid_argument("ContactID must be 32 bytes");
    }
    std::memcpy(value.data(), vec.data(), identitySize);
  }

  // Construct from Hex String
  explicit ContactID(const std::string_view hex) {
    if (hex.empty()) {
      return;
    }
    if (hex.size() != 2 * identitySize) {
      throw std::invalid_argument("Invalid hex string length for ContactID: " + std::string(hex));
    }
    for (std::uint8_t i = 0; i < identitySize; ++i) {
      value[i] = static_cast<uint8_t>(HexCharToInt(hex[2U * i]) << 4U | HexCharToInt(hex[(2U * i) + 1]));
    }
  }

  auto Value() const -> const std::array<uint8_t, identitySize>& {
    return value;
  }

  [[nodiscard]] auto ToString() const -> std::string {
    if (IsEmpty()) {
      return "";
    }
    std::string hex(2 * identitySize, '0');
    static constexpr std::array<char, 16> hexmap = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (size_t i = 0; i < identitySize; ++i) {
      hex[2U * i] = hexmap[(value[i] & 0xF0U) >> 4U];
      hex[(2U * i) + 1] = hexmap[value[i] & 0x0FU];
    }
    return hex;
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return std::ranges::all_of(value, [](uint8_t byte) { return byte == 0; });
  }

  auto operator<=>(const ContactID&) const = default;
  auto operator==(const ContactID&) const -> bool = default;

private:
  std::array<uint8_t, identitySize> value{};

  static constexpr auto HexCharToInt(char c) -> uint8_t {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    throw std::invalid_argument("Invalid hex char");
  }
};

// NOLINTNEXTLINE(*-identifier-naming)
inline void to_json(nlohmann::json& j, const ContactID& id) {
  j = id.ToString();
}

// NOLINTNEXTLINE(*-identifier-naming)
inline void from_json(const nlohmann::json& j, ContactID& id) {
  const auto strval = j.get<std::string>();
  if (strval.empty()) {
    id = ContactID();
  } else {
    id = ContactID(strval);
  }
}

inline auto operator<<(std::ostream& oss, const ContactID& id) -> std::ostream& {
  return oss << id.ToString();
}

}  // namespace eillance

namespace std {
template<>
struct hash<eillance::ContactID> {
  auto operator()(const eillance::ContactID& id) const -> size_t {
    // FNV-1a hash
    size_t hash = 2166136261u;
    for (auto b: id.Value()) {
      hash ^= b;
      hash *= 16777619u;
    }
    return hash;
  }
};

}  // namespace std
