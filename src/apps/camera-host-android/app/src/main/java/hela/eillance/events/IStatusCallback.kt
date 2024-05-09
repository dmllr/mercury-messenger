package hela.eillance.events

interface IStatusCallback {
  fun onStatusUpdate(source: Int, status: Int)
}
