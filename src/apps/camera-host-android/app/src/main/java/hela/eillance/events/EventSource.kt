package hela.eillance.events

enum class EventSource(val value: Int) {
  System(0),
  ConnectionStatus(1),
  GatheringState(2),
  IceState(3),
  SignalingState(4),
  Ping(5);

  companion object {
    fun fromInt(value: Int) = entries.first { it.value == value }
  }
}
