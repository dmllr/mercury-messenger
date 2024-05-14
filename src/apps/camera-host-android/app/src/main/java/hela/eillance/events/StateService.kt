package hela.eillance.events

import android.content.Context
import hela.eillance.camera.R

enum class StateService(val value: Int) {
  Unknown(0),
  Standby(1),
  Setup(2),
  Action(3),
  Failure(4);

  companion object {
    val resourceMap = mapOf(
      Unknown to R.string.state_service_unknown,
      Standby to R.string.state_service_standby,
      Setup to R.string.state_service_setup,
      Action to R.string.state_service_action,
      Failure to R.string.state_service_failure,
    )
    fun fromInt(value: Int?) = entries.first { it.value == value }

  }
}

fun StateService.getResourceString(context: Context): String {
  val resourceId = StateService.resourceMap[this] ?: R.string.unknown
  return context.getString(resourceId)
}
