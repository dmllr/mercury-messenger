package hela.eillance.ndkio.interfaces

import hela.eillance.ndkio.Peer
import hela.eillance.ndkio.Profile

interface IClientNativeBridge {
  fun onStatusUpdate(peerId: String, source: Int, status: Int)
  fun onStreamInfoChange(width: Int, height: Int, rotation: Int)
  fun onDeviceProfileReceived(peerId: String, profile: Profile)
  fun onPeerListUpdate(peers: List<Peer>)
}
