package hela.eillance.ndkio

import hela.eillance.events.StateIce
import hela.eillance.events.StateIceGathering
import hela.eillance.events.StatePeerConnection
import hela.eillance.events.StateService
import hela.eillance.events.StateSignaling

class Status {
  var connectionState: StateService = StateService.Unknown
  var peerConnectionState: StatePeerConnection = StatePeerConnection.New
  var iceState: StateIce = StateIce.New
  var iceGatheringState: StateIceGathering = StateIceGathering.New
  var signalingState: StateSignaling = StateSignaling.Unknown

  companion object {
    @JvmStatic
    fun fromInt(
      connectionState: Int,
      peerConnectionState: Int,
      iceState: Int,
      iceGatheringState: Int,
      signalingState: Int
    ): Status {
      return Status().apply {
        this.connectionState = StateService.fromInt(connectionState)
        this.peerConnectionState = StatePeerConnection.fromInt(peerConnectionState)
        this.iceState = StateIce.fromInt(iceState)
        this.iceGatheringState = StateIceGathering.fromInt(iceGatheringState)
        this.signalingState = StateSignaling.fromInt(signalingState)
      }
    }
  }
}
