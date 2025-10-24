#pragma once

#include <cstdint>

namespace eillance::transport {

// clang-format off
enum class PeerConnectionState : std::uint8_t {
  New,
  Connecting,
  Connected,
  Disconnected,
  Failed,
  Closed,
};
// clang-format on

}  // namespace eillance::transport
