package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class StatePeerConnection(val value: Int) {
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
}

fun StatePeerConnection.getResourceString(context: Context): String {
  val resourceId = StatePeerConnection.resourceMap[this] ?: R.string.unknown
  return context.getString(resourceId)
}
