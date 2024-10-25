#pragma once

#include <cstdint>

namespace hela::eillance {

enum class EventSource : std::uint8_t {
  system = 0,
  peerConnectionState,
  gatheringState,
  iceState,
  signalingState,
  ping,
};

}  // namespace hela::eillance
