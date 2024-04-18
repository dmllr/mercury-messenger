package me.hela.surveillance.events

interface IStatusCallback {
  fun onStatusUpdate(source: Int, status: Int)
}
