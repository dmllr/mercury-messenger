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
  std::array<uint8_t, 32> value{};

  constexpr ContactID() = default;

  explicit ContactID(const std::array<uint8_t, 32>& v) : value(v) {}

  // Construct from raw bytes
  explicit ContactID(const std::vector<std::byte>& v) {
    if (v.size() != 32) { throw std::invalid_argument("ContactID must be 32 bytes"); }
    std::memcpy(value.data(), v.data(), 32);
  }

  // Construct from Hex String
  explicit ContactID(std::string_view hex) {
    if (hex.empty()) { return; }  // Default constructed (zero)
    if (hex.size() != 64) { throw std::invalid_argument("Invalid hex string length for ContactID: " + std::string(hex)); }
    for (size_t i = 0; i < 32; ++i) {
      value[i] = static_cast<uint8_t>(HexCharToInt(hex[2 * i]) << 4 | HexCharToInt(hex[2 * i + 1]));
    }
  }

  [[nodiscard]] auto ToString() const -> std::string {
    if (IsEmpty()) return "";
    std::string hex(64, '0');
    static constexpr char hexmap[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
      hex[2 * i] = hexmap[(value[i] & 0xF0) >> 4];
      hex[2 * i + 1] = hexmap[value[i] & 0x0F];
    }
    return hex;
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    for (auto b: value) {
      if (b != 0) return false;
    }
    return true;
  }

  auto operator<=>(const ContactID&) const = default;
  auto operator==(const ContactID&) const -> bool = default;

private:
  static constexpr auto HexCharToInt(char c) -> uint8_t {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    throw std::invalid_argument("Invalid hex char");
  }
};

inline void to_json(nlohmann::json& j, const ContactID& id) {
  j = id.ToString();
}

inline void from_json(const nlohmann::json& j, ContactID& id) {
  const auto s = j.get<std::string>();
  if (s.empty()) {
    id = ContactID();
  } else {
    id = ContactID(s);
  }
}

inline auto operator<<(std::ostream& os, const ContactID& id) -> std::ostream& {
  return os << id.ToString();
}

}  // namespace eillance

namespace std {
template<>
struct hash<eillance::ContactID> {
  auto operator()(const eillance::ContactID& id) const -> size_t {
    // FNV-1a hash
    size_t hash = 2166136261u;
    for (auto b: id.value) {
      hash ^= b;
      hash *= 16777619u;
    }
    return hash;
  }
};
}  // namespace std
