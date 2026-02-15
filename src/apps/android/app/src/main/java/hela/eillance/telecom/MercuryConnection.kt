package hela.eillance.telecom

import android.net.Uri
import android.telecom.CallAudioState
import android.telecom.Connection
import android.telecom.DisconnectCause
import android.telecom.PhoneAccount
import android.telecom.TelecomManager
import android.util.Log
import hela.eillance.MessengerService

class MercuryConnection(
  private val messengerService: MessengerService,
  private val peerId: String,
  private val isIncoming: Boolean
) : Connection() {

  init {
    connectionProperties = PROPERTY_SELF_MANAGED
    // CRITICAL FIX: For incoming calls, start with audioModeIsVoip = false.
    // This allows the Ringtone to play on the RING stream (System Ringer Volume).
    // If set to true immediately, Android forces MODE_IN_COMMUNICATION, using the
    // VOICE_CALL stream for the ringtone, which is often too quiet or routed to the earpiece.
    audioModeIsVoip = !isIncoming

    connectionCapabilities = CAPABILITY_MUTE or CAPABILITY_SUPPORT_HOLD or CAPABILITY_HOLD

    setCallerDisplayName(peerId, TelecomManager.PRESENTATION_ALLOWED)
    setAddress(Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null), TelecomManager.PRESENTATION_ALLOWED)

    if (isIncoming) {
      setRinging()
    } else {
      setInitializing()
      setDialing()
    }
  }

  override fun onStateChanged(state: Int) {
    super.onStateChanged(state)
    // Ensure we switch to VoIP mode (IN_COMMUNICATION) as soon as the call becomes active
    // This routes audio to the earpiece/headset properly for the conversation.
    if (state == STATE_ACTIVE) {
      audioModeIsVoip = true
    }
  }

  override fun onShowIncomingCallUi() {
    Log.i(TAG, "onShowIncomingCallUi for $peerId")
    // Delegate entirely to CallService.
    messengerService.showIncomingCallNotification(peerId)
  }

  override fun onCallAudioStateChanged(state: CallAudioState?) {
    Log.i(TAG, "onCallAudioStateChanged: $state")
    // Propagate speakerphone state changes to the UI/Service if needed
    // Note: WebRTC/CallService usually manages this via AudioManager directly,
    // but observing this helps keep state in sync if changed via system UI.
    if (state != null) {
      val isSpeaker = (state.route == CallAudioState.ROUTE_SPEAKER)
      // We don't force it back here to avoid loops, but we log it.
    }
  }

  override fun onAnswer(videoState: Int) {
    Log.i(TAG, "onAnswer(videoState=$videoState) from Telecom (Watch/Headset)")
    handleAnswer()
  }

  override fun onAnswer() {
    Log.i(TAG, "onAnswer from Telecom (Watch/Headset)")
    handleAnswer()
  }

  private fun handleAnswer() {
    // 1. Switch audio mode immediately so the system knows we are now in a call.
    audioModeIsVoip = true

    // 2. Tell Telecom we are active. This updates the smartwatch UI immediately.
    setActive()

    // 3. Tell the Engine/Service to accept the call logic (WebRTC negotiation).
    messengerService.acceptCall()
  }

  override fun onReject() {
    Log.i(TAG, "onReject from Telecom")
    // 1. Tell Telecom we are disconnected immediately.
    setDisconnected(DisconnectCause(DisconnectCause.REJECTED))

    // 2. Tell Service to run rejection logic (stop ringing, send reject msg).
    messengerService.rejectCall()

    // 3. Destroy this connection.
    destroy()
  }

  override fun onDisconnect() {
    Log.i(TAG, "onDisconnect from Telecom")
    setDisconnected(DisconnectCause(DisconnectCause.LOCAL))
    messengerService.onTelecomDisconnect(this)
    destroy()
  }

  override fun onAbort() {
    Log.i(TAG, "onAbort")
    super.onAbort()
    messengerService.onTelecomDisconnect(this)
    destroy()
  }

  companion object {
    private const val TAG = "MercuryConnection"
  }
}
