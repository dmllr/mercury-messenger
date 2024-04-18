package me.hela.surveillance.events

enum class EventSource(val value: Int) {
  System(0),
  RTC(1),
  RTSP(2);

  companion object {
    fun fromInt(value: Int) = EventSource.values().first { it.value == value }
  }
}
