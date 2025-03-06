package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class PeerStateIce(val value: Int) {
  New(0),
  Checking(1),
  Connected(2),
  Completed(3),
  Failed(4),
  Disconnected(5),
  Closed(6),
  Unknown(99);

  companion object {
    val resourceMap = mapOf(
      New to R.string.state_ice_new,
      Checking to R.string.state_ice_checking,
      Connected to R.string.state_ice_connected,
      Completed to R.string.state_ice_completed,
      Failed to R.string.state_ice_failed,
      Disconnected to R.string.state_ice_disconnected,
      Closed to R.string.state_ice_closed,
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
