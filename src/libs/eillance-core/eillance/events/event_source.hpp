#pragma once

#include <cstdint>

namespace eillance {

enum class EventSource : std::uint8_t {
  system = 0,
  peerConnectionState,
  mediaConnectionState,
  gatheringState,
  iceState,
  signalingState,
  ping,
};

}  // namespace eillance
