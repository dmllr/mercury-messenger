package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class PeerStateLink(val value: Int) {
  New(0),
  Connecting(1),
  Connected(2),
  Disconnected(3),
  Failed(4),
  Closed(5);

  companion object {
    val resourceMap = mapOf(
      New to R.string.state_peer_connection_new,
      Connecting to R.string.state_peer_connection_connecting,
      Connected to R.string.state_peer_connection_connected,
      Disconnected to R.string.state_peer_connection_disconnected,
      Failed to R.string.state_peer_connection_failed,
      Closed to R.string.state_peer_connection_closed,
    )

    fun fromInt(value: Int?) = entries.first { it.value == value }
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: R.string.fr_operation_status_unknown
    return context.getString(resourceId)
  }
}
