package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class StateSignalling(val value: Int) {
  Stable(0),
  HaveLocalOffer(1),
  HaveRemoteOffer(2),
  HaveLocalPranswer(3),
  HaveRemotePranswer(4),
  Unknown(99);

  companion object {
    val resourceMap = mapOf(
      Stable to R.string.state_signalling_stable,
      HaveLocalOffer to R.string.state_signalling_have_local_offer,
      HaveRemoteOffer to R.string.state_signalling_have_remote_offer,
      HaveLocalPranswer to R.string.state_signalling_have_local_pranswer,
      HaveRemotePranswer to R.string.state_signalling_have_remote_pranswer,
      Unknown to R.string.unknown,
    )
    fun fromInt(value: Int?) = entries.first { it.value == value }
  }
}

fun StateSignalling.getResourceString(context: Context): String {
  val resourceId = StateSignalling.resourceMap[this] ?: R.string.unknown
  return context.getString(resourceId)
}
