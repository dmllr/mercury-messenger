package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class StateIceGathering(val value: Int) {
  New(0),
  InProgress(1),
  Complete(2);

  companion object {
    val resourceMap = mapOf(
      New to R.string.state_ice_gathering_new,
      InProgress to R.string.state_ice_gathering_in_progress,
      Complete to R.string.state_ice_gathering_complete,
    )
    fun fromInt(value: Int?) = entries.first { it.value == value }
  }
}

fun StateIceGathering.getResourceString(context: Context): String {
  val resourceId = StateIceGathering.resourceMap[this] ?: R.string.unknown
  return context.getString(resourceId)
}
