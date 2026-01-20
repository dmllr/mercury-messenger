#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <compare>
#include <nlohmann/json.hpp>
#include <span>

namespace eillance {

struct ContactID {
  static constexpr auto identitySize = 32U;

  constexpr ContactID() = default;

  explicit ContactID(std::array<std::byte, identitySize> const& value)
      : value_(value) {}

  // Construct from raw bytes
  explicit ContactID(std::vector<std::byte> const& vec) {
    if (vec.size() != identitySize) {
      throw std::invalid_argument("ContactID must be 32 bytes");
    }
    std::ranges::copy(vec, value_.begin());
  }

  explicit ContactID(std::span<std::byte const> span) {
    if (span.size() != identitySize) {
      throw std::invalid_argument("ContactID must be 32 bytes");
    }
    std::ranges::copy(span, value_.begin());
  }

  // Construct from Hex String
  explicit ContactID(std::string_view const hex) {
    if (hex.empty()) {
      return;
    }
    if (hex.size() != 2U * identitySize) {
      throw std::invalid_argument("Invalid hex string length for ContactID: " + std::string(hex));
    }
    for (std::uint8_t i = 0; i < identitySize; ++i) {
      value_[i] = static_cast<std::byte>((HexCharToInt(hex[2U * i]) << 4U) | HexCharToInt(hex[(2U * i) + 1]));
    }
  }

  [[nodiscard]] auto Value() const -> std::array<std::byte, identitySize> const& { return value_; }

  [[nodiscard]] auto Span() const -> std::span<std::byte const> { return value_; }

  [[nodiscard]] auto ToString() const -> std::string {
    if (IsEmpty()) {
      return "";
    }
    std::string hex(2 * identitySize, '0');
    static constexpr std::array<char, 16> hexmap =
        {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (size_t i = 0; i < identitySize; ++i) {
      hex[2U * i] = hexmap[(static_cast<uint8_t>(value_[i]) & 0xF0U) >> 4U];
      hex[(2U * i) + 1] = hexmap[static_cast<uint8_t>(value_[i]) & 0x0FU];
    }
    return hex;
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return std::ranges::all_of(value_, [](std::byte byte) { return byte == std::byte{0}; });
  }

  auto operator<=>(ContactID const&) const = default;
  auto operator==(ContactID const&) const -> bool = default;

 private:
  std::array<std::byte, identitySize> value_{};

  static constexpr auto HexCharToInt(char chr) -> uint8_t {
    if (chr >= '0' && chr <= '9') {
      return chr - '0';
    }
    if (chr >= 'a' && chr <= 'f') {
      return chr - 'a' + 10;
    }
    if (chr >= 'A' && chr <= 'F') {
      return chr - 'A' + 10;
    }
    throw std::invalid_argument("Invalid hex char");
  }
};

// NOLINTNEXTLINE(*-identifier-naming)
inline void to_json(nlohmann::json& json, ContactID const& cid) { json = cid.ToString(); }

// NOLINTNEXTLINE(*-identifier-naming)
inline void from_json(nlohmann::json const& json, ContactID& cid) {
  auto const strval = json.get<std::string>();
  if (strval.empty()) {
    cid = ContactID();
  } else {
    cid = ContactID(strval);
  }
}

inline auto operator<<(std::ostream& oss, ContactID const& cid) -> std::ostream& { return oss << cid.ToString(); }

}  // namespace eillance

namespace std {
template <>
struct hash<eillance::ContactID> {
  auto operator()(eillance::ContactID const& id) const -> size_t {
    // FNV-1a hash
    auto hash = 2166136261U;
    for (auto byte : id.Value()) {
      hash ^= static_cast<size_t>(byte);
      hash *= 16777619U;
    }
    return hash;
  }
};

}  // namespace std
