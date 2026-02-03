package hela.eillance.ndkio

import android.content.Context
import hela.eillance.camera.R

enum class PeerConnectionState(val value: Int) {
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

    @JvmStatic
    fun fromInt(value: Int) =
      entries.firstOrNull { it.value == value } ?: throw Exception("Invalid PeerConnectionState value: $value")
  }

  @Suppress("unused")
  fun getResourceString(context: Context): String {
    val resourceId = resourceMap[this] ?: throw Exception("Invalid PeerConnectionState value: $this")
    return context.getString(resourceId)
  }
}
