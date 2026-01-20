#pragma once

#include <cstdint>

namespace eillance::transport {

enum class PeerConnectionState : std::uint8_t {
  newConnection,
  connecting,
  connected,
  disconnected,
  failed,
  closed,
};

}  // namespace eillance::transport
