#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <span>

namespace eillance {

struct GroupID {
  static constexpr auto identitySize = 32U;

  constexpr GroupID() = default;

  explicit GroupID(std::span<std::byte const> span) {
    if (span.empty()) return;
    if (span.size() != identitySize) throw std::invalid_argument("GroupID must be 32 bytes");
    std::ranges::copy(span, value_.begin());
  }

  explicit GroupID(std::string_view const hex) {
    if (hex.empty()) return;
    if (hex.size() != 2U * identitySize) throw std::invalid_argument("Invalid hex string length for GroupID");
    for (std::size_t i = 0; i < identitySize; ++i) {
      auto const high = HexCharToInt(hex[2 * i]);
      auto const low = HexCharToInt(hex[2 * i + 1]);
      value_[i] = static_cast<std::byte>((high << 4U) | low);
    }
  }

  [[nodiscard]] auto Span() const -> std::span<std::byte const> {
    return value_;
  }

  [[nodiscard]] auto ToString() const -> std::string {
    if (IsEmpty()) return "";
    std::string hex(2 * identitySize, '0');
    static constexpr std::array<char, 16> hexmap =
        {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (size_t i = 0; i < identitySize; ++i) {
      hex[2U * i] = hexmap[(static_cast<uint8_t>(value_[i]) & 0xF0U) >> 4U];
      hex[(2U * i) + 1] = hexmap[static_cast<uint8_t>(value_[i]) & 0x0FU];
    }
    return hex;
  }

  [[nodiscard]] auto AsContactID() const -> ContactID {
    return ContactID(value_);
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return std::ranges::all_of(value_, [](std::byte byte) { return byte == std::byte{0}; });
  }

  auto operator<=>(GroupID const&) const = default;
  auto operator==(GroupID const&) const -> bool = default;

 private:
  std::array<std::byte, identitySize> value_{};
  static constexpr auto HexCharToInt(char const chr) -> uint8_t {
    auto const c = static_cast<unsigned char>(chr);
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    throw std::invalid_argument(std::string("Invalid hex char: ") + chr);
  }
};

inline void to_json(nlohmann::json& json, GroupID const& gid) {
  json = gid.ToString();
}
inline void from_json(nlohmann::json const& json, GroupID& gid) {
  auto const strval = json.get<std::string>();
  if (strval.empty())
    gid = GroupID();
  else
    gid = GroupID(strval);
}
}  // namespace eillance

template <>
struct std::hash<eillance::GroupID> {
  auto operator()(eillance::GroupID const& id) const noexcept -> size_t {
    auto hash = 2166136261U;
    for (auto byte : id.Span()) {
      hash ^= static_cast<size_t>(byte);
      hash *= 16777619U;
    }
    return hash;
  }
};
