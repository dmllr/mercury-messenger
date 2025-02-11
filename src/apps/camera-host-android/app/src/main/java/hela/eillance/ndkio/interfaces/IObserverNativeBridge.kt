package hela.eillance.ndkio.interfaces

import hela.eillance.ndkio.ClientInfo

interface IObserverNativeBridge {
  fun onStreamResolutionChange(width: Int, height: Int)
  fun onClientListUpdate(clients: List<ClientInfo>)
}
