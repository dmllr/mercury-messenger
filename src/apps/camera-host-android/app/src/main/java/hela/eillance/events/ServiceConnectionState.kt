package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class ServiceConnectionState(val value: Int) {
  Unknown(0),
  Establishing(1),
  Online(2),
  Failure(3),
  Disconnected(4);

  companion object {
    val resourceMap = mapOf(
      Unknown to R.string.service_connection_state_unknown,
      Establishing to R.string.service_connection_state_establishing,
      Online to R.string.service_connection_state_online,
      Failure to R.string.service_connection_state_failure,
      Disconnected to R.string.service_connection_state_disconnected,
    )

    @JvmStatic
    fun fromInt(value: Int) = entries.first { it.value == value }
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: R.string.service_connection_state_unknown
    return context.getString(resourceId)
  }
}
