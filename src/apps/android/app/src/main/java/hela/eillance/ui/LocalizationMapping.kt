package hela.eillance.ui

import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.HistoryEntryKind

/**
 * Maps Core data types to generated Resource IDs.
 */
object LocalizationMapping {

  fun getCallDescriptionResId(kind: HistoryEntryKind, direction: Int): Int {
    // direction: 0 = Outgoing, 1 = Incoming (from CallDirection enum values)
    val isIncoming = direction == CallDirection.Incoming.value

    return when (kind) {
      HistoryEntryKind.VoiceCall -> if (isIncoming) R.string.call_incoming_voice else R.string.call_outgoing_voice
      HistoryEntryKind.VideoCall -> if (isIncoming) R.string.call_incoming_video else R.string.call_outgoing_video
      else -> R.string.call_generic
    }
  }

  fun getCallStatusResId(status: hela.eillance.ndkio.CallStatus): Int {
    return when (status) {
      hela.eillance.ndkio.CallStatus.Missed -> R.string.call_missed
      hela.eillance.ndkio.CallStatus.Rejected -> R.string.call_rejected
      else -> R.string.call_generic
    }
  }

  fun getSystemMessageResId(action: String?): Int {
    return when (action) {
      "contact_request", "contact_request_received" -> R.string.sys_contact_req
      "contact_confirm", "contact_signature" -> R.string.sys_contact_confirm
      "profile_update", "profile_update_diff", "send_profile" -> R.string.sys_profile_update
      "unsupported_format" -> R.string.sys_unsupported
      "call_request" -> R.string.call_generic
      "call_accept" -> R.string.call_generic
      "call_missed" -> R.string.call_missed
      "call_rejected" -> R.string.call_rejected
      else -> R.string.sys_default
    }
  }
}
