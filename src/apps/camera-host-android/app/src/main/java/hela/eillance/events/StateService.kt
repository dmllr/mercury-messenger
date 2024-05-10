package hela.eillance.events

import hela.eillance.camera.R

enum class StateService(val value: Int) {
  Unknown(0),
  Standby(1),
  Setup(2),
  Action(3),
  Failure(4);

  fun toResourceId(state: StateService): Int {
    return when (state) {
      Unknown -> R.string.state_service_unknown
      Standby -> R.string.state_service_standby
      Setup -> R.string.state_service_setup
      Action -> R.string.state_service_action
      Failure -> R.string.state_service_failure
    }
  }

  companion object {
    fun fromInt(value: Int?) = entries.first { it.value == value }

  }
}
