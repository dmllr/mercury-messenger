package hela.eillance.events

interface IStatusUpdateCallback {
  fun onStatusUpdate(source: Int, status: Int)
}
