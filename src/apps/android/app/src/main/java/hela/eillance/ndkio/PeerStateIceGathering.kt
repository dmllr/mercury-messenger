package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class PeerStateIceGathering(val value: Int) {
  New(0),
  InProgress(1),
  Complete(2),
  Unknown(99);

  companion object {
    val resourceMap = mapOf(
      New to R.string.state_ice_gathering_new,
      InProgress to R.string.state_ice_gathering_in_progress,
      Complete to R.string.state_ice_gathering_complete,
    )

    @JvmStatic
    fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Unknown
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: R.string.fr_operation_status_unknown
    return context.getString(resourceId)
  }
}
