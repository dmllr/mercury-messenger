package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class PeerStateSignaling(val value: Int) {
  Stable(0),
  HaveLocalOffer(1),
  HaveRemoteOffer(2),
  HaveLocalPranswer(3),
  HaveRemotePranswer(4),
  Unknown(99);

  companion object {
    val resourceMap = mapOf(
      Stable to R.string.state_signaling_stable,
      HaveLocalOffer to R.string.state_signaling_have_local_offer,
      HaveRemoteOffer to R.string.state_signaling_have_remote_offer,
      HaveLocalPranswer to R.string.state_signaling_have_local_pranswer,
      HaveRemotePranswer to R.string.state_signaling_have_remote_pranswer,
      Unknown to R.string.fr_operation_status_unknown,
    )

    fun fromInt(value: Int?) = entries.first { it.value == value }
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: R.string.fr_operation_status_unknown
    return context.getString(resourceId)
  }
}
