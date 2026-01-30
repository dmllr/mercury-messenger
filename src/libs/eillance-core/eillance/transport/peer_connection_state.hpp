#pragma once

#include <cstdint>

namespace eillance::transport {

enum class PeerConnectionState : std::uint8_t {
  // NOLINTNEXTLINE
  new_ [[maybe_unused]] = 0,  // unused, but shall remain to map WebRTC's PeerConnectionState::NEW
  connecting = 1,
  connected = 2,
  disconnected = 3,
  failed = 4,
  closed = 5,
};

}  // namespace eillance::transport
