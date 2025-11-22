package hela.eillance.telecom

import android.content.Intent
import android.net.Uri
import android.telecom.CallAudioState
import android.telecom.Connection
import android.telecom.DisconnectCause
import android.telecom.PhoneAccount
import android.telecom.TelecomManager
import android.util.Log
import hela.eillance.CallService
import hela.eillance.MainActivity

class MercuryConnection(
  private val callService: CallService,
  private val peerId: String,
  private val isIncoming: Boolean
) : Connection() {

  init {
    // PROPERTY_SELF_MANAGED requires the app to handle:
    // 1. The UI (Incoming call screen, In-call screen)
    // 2. The Audio (Playing ringtone, Ringback tone)
    connectionProperties = PROPERTY_SELF_MANAGED
    audioModeIsVoip = true
    connectionCapabilities = CAPABILITY_MUTE or CAPABILITY_SUPPORT_HOLD

    setCallerDisplayName(peerId, TelecomManager.PRESENTATION_ALLOWED)
    setAddress(Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null), TelecomManager.PRESENTATION_ALLOWED)

    // If this is an outgoing call, we initialize it as initializing.
    // The Service will set it to Dialing/Active.
    if (!isIncoming) {
      setInitializing()
    }
  }

  override fun onShowIncomingCallUi() {
    Log.i(TAG, "onShowIncomingCallUi for $peerId")
    // Launch the custom Incoming Call UI.
    // Note: The ringtone is played by CallSoundPlayer in CallService based on the 'Ringing' state.
    val intent = Intent(callService, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
      action = CallService.ACTION_SHOW_INCOMING_CALL
      putExtra(CallService.EXTRA_PEER_ID, peerId)
    }
    callService.startActivity(intent)
  }

  override fun onCallAudioStateChanged(state: CallAudioState?) {
    Log.i(TAG, "onCallAudioStateChanged: $state")
    // ConnectionService handles audio focus and routing (Bluetooth/Speaker),
    // but WebRTC handles the actual audio stream I/O.
  }

  override fun onAnswer() {
    Log.i(TAG, "onAnswer")
    callService.acceptCall()
    setActive()
  }

  override fun onReject() {
    Log.i(TAG, "onReject")
    callService.rejectCall()
    setDisconnected(DisconnectCause(DisconnectCause.REJECTED))
    destroy()
  }

  override fun onDisconnect() {
    Log.i(TAG, "onDisconnect")
    callService.hangup()
    setDisconnected(DisconnectCause(DisconnectCause.LOCAL))
    destroy()
  }

  override fun onHold() {
    Log.i(TAG, "onHold")
    // Implement hold logic if supported by C++ engine
    setOnHold()
  }

  override fun onUnhold() {
    Log.i(TAG, "onUnhold")
    setActive()
  }

  override fun onAbort() {
    Log.i(TAG, "onAbort")
    super.onAbort()
    destroy()
  }

  fun onRemoteHangup() {
    setDisconnected(DisconnectCause(DisconnectCause.REMOTE))
    destroy()
  }

  fun onEstablished() {
    setActive()
  }

  companion object {
    private const val TAG = "MercuryConnection"
  }
}
