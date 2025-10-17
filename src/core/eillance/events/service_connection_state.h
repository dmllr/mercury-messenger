#pragma once

#include <cstdint>

namespace eillance {

enum class ServiceConnectionState : std::uint8_t {
  unknown = 0,
  establishing,
  online,
  failure,
  disconnected,
};

}  // namespace eillance
