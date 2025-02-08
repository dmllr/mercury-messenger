package hela.eillance.ndkio.interfaces

interface INativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
}
