#pragma once

namespace hela::eillance {

enum class EventSource : int {
  System = 0,
  ConnectionStatus,
  GatheringState,
  IceState,
  SignalingState,
};

}  // namespace hela::eillance
