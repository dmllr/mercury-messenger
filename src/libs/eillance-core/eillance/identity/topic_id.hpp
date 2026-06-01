#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include <compare>

#include "eillance/identity/channel_id.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/group_id.hpp"

namespace eillance {

struct TopicID {
  static constexpr auto idSize = 32U;
  std::array<std::byte, idSize> value_{};

  TopicID() = default;
  explicit TopicID(ContactID const& c) {
    std::ranges::copy(c.Span(), value_.begin());
  }
  explicit TopicID(GroupID const& g) {
    std::ranges::copy(g.Span(), value_.begin());
  }
  explicit TopicID(ChannelID const& c) {
    std::ranges::copy(c.Span(), value_.begin());
  }
  explicit TopicID(std::string_view hex) {
    if (hex.size() == 2 * idSize) {
      auto cid = ContactID(hex);
      std::ranges::copy(cid.Span(), value_.begin());
    }
  }

  [[nodiscard]] auto Span() const -> std::span<std::byte const> {
    return value_;
  }

  [[nodiscard]] auto ToString() const -> std::string {
    return ContactID(value_).ToString();
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return ContactID(value_).IsEmpty();
  }

  auto operator<=>(TopicID const& other) const = default;
  auto operator==(TopicID const& other) const -> bool = default;
};

}  // namespace eillance

template <>
struct std::hash<eillance::TopicID> {
  auto operator()(eillance::TopicID const& id) const noexcept -> size_t {
    auto hash = 2166136261U;
    for (auto byte : id.Span()) {
      hash ^= static_cast<size_t>(byte);
      hash *= 16777619U;
    }
    return hash;
  }
};
