package hela.eillance.events

enum class ServiceState(val value: Int) {
  Unknown(0),
  Standby(1),
  Setup(2),
  Action(3),
  Failure(4);

  companion object {
    fun fromInt(value: Int) = entries.first { it.value == value }
  }
}
