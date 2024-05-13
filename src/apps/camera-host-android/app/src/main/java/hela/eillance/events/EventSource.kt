package hela.eillance.events

enum class EventSource(val value: Int) {
  System(0),
  ConnectionStatus(1),
  PeerConnectionState(2),
  GatheringState(3),
  IceState(4),
  SignalingState(5),
  Ping(6);

  companion object {
    fun fromInt(value: Int) = entries.first { it.value == value }
  }
}
