#pragma once

namespace hela::eillance {

enum class EventSource : int {
  System = 0,
  ConnectionStatus,
  PeerConnectionState,
  GatheringState,
  IceState,
  SignalingState,
  Ping,
};

}  // namespace hela::eillance
