#pragma once

namespace hela::eillance {

enum class ServiceState : int {
  unknown = 0,
  standby,
  setup,
  action,
  failure,
};

}  // namespace hela::eillance
