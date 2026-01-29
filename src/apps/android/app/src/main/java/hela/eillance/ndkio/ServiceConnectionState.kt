package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class ServiceConnectionState(val value: Int) {
  Unknown(0),
  Establishing(1),
  Online(2),
  Failure(3),
  Disconnected(4),
  InvalidToken(5),
  NoNetwork(6);

  companion object {
    val resourceMap = mapOf(
      Unknown to R.string.service_connection_state_unknown,
      Establishing to R.string.service_connection_state_establishing,
      Online to R.string.service_connection_state_online,
      Failure to R.string.service_connection_state_failure,
      Disconnected to R.string.service_connection_state_disconnected,
      InvalidToken to R.string.service_connection_state_invalid_token,
      NoNetwork to R.string.service_connection_state_failure
    )

    @JvmStatic
    fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Unknown
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    if (this == NoNetwork) return "Waiting for network"
    val resourceId = resourceMap[this] ?: R.string.service_connection_state_unknown
    return context.getString(resourceId)
  }
}
