package hela.eillance.ndkio.interfaces

interface IPerformerNativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
}
