package hela.eillance.telecom

import android.net.Uri
import android.telecom.CallAudioState
import android.telecom.Connection
import android.telecom.PhoneAccount
import android.telecom.TelecomManager
import android.util.Log
import hela.eillance.CallService

class MercuryConnection(
  private val callService: CallService,
  private val peerId: String,
  private val isIncoming: Boolean
) : Connection() {

  init {
    connectionProperties = PROPERTY_SELF_MANAGED
    audioModeIsVoip = true
    connectionCapabilities = CAPABILITY_MUTE or CAPABILITY_SUPPORT_HOLD

    setCallerDisplayName(peerId, TelecomManager.PRESENTATION_ALLOWED)
    setAddress(Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null), TelecomManager.PRESENTATION_ALLOWED)

    if (!isIncoming) {
      setInitializing()
    }
  }

  override fun onShowIncomingCallUi() {
    Log.i(TAG, "onShowIncomingCallUi for $peerId")
    // Delegate entirely to CallService.
    // This ensures that the Service is the single source of truth for notifications.
    callService.showIncomingCallNotification(peerId)
  }

  @Deprecated("Deprecated in Java")
  override fun onCallAudioStateChanged(state: CallAudioState?) {
    // Audio focus managed by ConnectionService, routing managed by WebRTC
  }

  override fun onAnswer() {
    Log.i(TAG, "onAnswer")
    callService.acceptCall()
    setActive()
  }

  override fun onReject() {
    Log.i(TAG, "onReject")
    // This comes from the System UI or Notification action provided to Telecom
    // We notify the service to run cleanup and native logic.
    callService.rejectCall()
    // NOTE: CallService.rejectCall() will eventually call destroy() on us,
    // but we set status here to be sure.
    setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.REJECTED))
    destroy()
  }

  override fun onDisconnect() {
    Log.i(TAG, "onDisconnect")
    callService.onTelecomDisconnect(this)
    setDisconnected(android.telecom.DisconnectCause(android.telecom.DisconnectCause.LOCAL))
    destroy()
  }

  override fun onAbort() {
    Log.i(TAG, "onAbort")
    super.onAbort()
    callService.onTelecomDisconnect(this)
    destroy()
  }

  companion object {
    private const val TAG = "MercuryConnection"
  }
}
