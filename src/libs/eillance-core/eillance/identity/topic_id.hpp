#pragma once

#include <string>
#include <string_view>

#include <compare>

#include "eillance/identity/contact_id.hpp"

namespace eillance {

struct TopicID {
  ContactID value;

  TopicID() = default;
  explicit TopicID(ContactID const& c)
      : value(c) {
  }
  explicit TopicID(std::string_view hex)
      : value(hex) {
  }

  [[nodiscard]] auto ToString() const -> std::string {
    return value.ToString();
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return value.IsEmpty();
  }

  auto operator<=>(TopicID const&) const = default;
  auto operator==(TopicID const&) const -> bool = default;
};

}  // namespace eillance
