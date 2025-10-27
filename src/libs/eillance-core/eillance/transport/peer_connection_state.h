#pragma once

#include <cstdint>

namespace eillance::transport {

enum class PeerConnectionState : std::uint8_t {
  NewConnection,
  Connecting,
  Connected,
  Disconnected,
  Failed,
  Closed,
};

}  // namespace eillance::transport
