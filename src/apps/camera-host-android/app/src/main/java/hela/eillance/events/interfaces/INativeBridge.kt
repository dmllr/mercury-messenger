package hela.eillance.events.interfaces

interface INativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
}
