package hela.eillance.events.interfaces

interface IStatusUpdateCallback {
  fun onStatusUpdate(source: Int, status: Int)
}
