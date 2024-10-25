#pragma once

#include <cstdint>

namespace hela::eillance {

enum class ServiceConnectionState : std::uint8_t {
  unknown = 0,
  establishing,
  online,
  failure,
};

}  // namespace hela::eillance
