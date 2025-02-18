package hela.eillance.ndkio.interfaces

interface IStreamerNativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
}
