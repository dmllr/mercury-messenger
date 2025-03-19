package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class ServiceConnectionState(val value: Int) {
  New(0),
  Establishing(1),
  Online(2),
  Disconnected(3),
  Failure(4),
  Closed(5),
  Unknown(99);

  companion object {
    val resourceMap = mapOf(
      New to R.string.service_connection_state_new,
      Establishing to R.string.service_connection_state_establishing,
      Online to R.string.service_connection_state_online,
      Disconnected to R.string.service_connection_state_disconnected,
      Failure to R.string.service_connection_state_failure,
      Closed to R.string.service_connection_state_closed,
    )

    @JvmStatic
    fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Unknown
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: R.string.service_connection_state_unknown
    return context.getString(resourceId)
  }
}
