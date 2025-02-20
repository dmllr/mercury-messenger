package hela.eillance.ndkio.interfaces

import hela.eillance.ndkio.Devices
import hela.eillance.ndkio.Profile

interface IViewerNativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
  fun onStreamResolutionChange(width: Int, height: Int)
  fun onPeerListUpdate(peers: Devices)
  fun onDeviceProfileReceived(peerId: String, profile: Profile)
}
