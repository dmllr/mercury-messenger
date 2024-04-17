package me.hela.surveillance

interface IStatusCallback {
  fun onStatusUpdate(source: Int, status: Int)
}
