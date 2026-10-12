#pragma once

#include <cstdint>

#include <compare>

#include "eillance/utils/time.hpp"

namespace eillance {

struct Timestamp {
  std::uint64_t value = 0;

  static auto Now() -> Timestamp {
    return {Time::Now()};
  }

  auto operator<=>(Timestamp const&) const = default;
  auto operator==(Timestamp const&) const -> bool = default;
};

}  // namespace eillance
