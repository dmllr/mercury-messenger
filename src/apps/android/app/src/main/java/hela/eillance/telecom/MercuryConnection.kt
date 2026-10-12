package hela.eillance.telecom

import android.net.Uri
import android.telecom.Connection
import android.telecom.DisconnectCause
import android.telecom.PhoneAccount
import android.telecom.TelecomManager
import hela.eillance.MessengerService

class MercuryConnection(
  private val messengerService: MessengerService,
  private val peerId: String,
  private val isIncoming: Boolean
) : Connection() {

  init {
    connectionProperties = PROPERTY_SELF_MANAGED
    // CRITICAL: For incoming calls, start with audioModeIsVoip = false.
    audioModeIsVoip = !isIncoming

    connectionCapabilities = CAPABILITY_MUTE or CAPABILITY_SUPPORT_HOLD or CAPABILITY_HOLD

    val contact = messengerService.resolveContactInfo(peerId)
    val displayName = if (!contact?.name.isNullOrBlank()) {
      "${contact.name} (${peerId.take(8)}...)"
    } else {
      "${peerId.take(8)}..."
    }
    setCallerDisplayName(displayName, TelecomManager.PRESENTATION_ALLOWED)
    setAddress(
      Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null),
      TelecomManager.PRESENTATION_ALLOWED
    )

    if (isIncoming) {
      setRinging()
    } else {
      setInitializing()
      setDialing()
    }
  }

  override fun onStateChanged(state: Int) {
    super.onStateChanged(state)
    if (state == STATE_ACTIVE) {
      audioModeIsVoip = true
    }
  }

  override fun onShowIncomingCallUi() {
    messengerService.showIncomingCallNotification(peerId)
  }

  override fun onAnswer(videoState: Int) {
    handleAnswer()
  }

  override fun onAnswer() {
    handleAnswer()
  }

  private fun handleAnswer() {
    audioModeIsVoip = true  // so the system knows we are now in a call
    setActive()
    messengerService.acceptCall()
  }

  override fun onReject() {
    setDisconnected(DisconnectCause(DisconnectCause.REJECTED))
    messengerService.rejectCall()
    destroy()
  }

  override fun onDisconnect() {
    setDisconnected(DisconnectCause(DisconnectCause.LOCAL))
    messengerService.onTelecomDisconnect(this)
    destroy()
  }

  override fun onAbort() {
    super.onAbort()
    messengerService.onTelecomDisconnect(this)
    destroy()
  }
}
