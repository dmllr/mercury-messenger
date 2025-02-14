package hela.eillance.ndkio.interfaces

import hela.eillance.ndkio.Devices
import hela.eillance.ndkio.Profile

interface IObserverNativeBridge {
  fun onStreamResolutionChange(width: Int, height: Int)
  fun onClientListUpdate(clients: Devices)
  fun onDeviceProfileReceived(peerId: String, profile: Profile)
}
