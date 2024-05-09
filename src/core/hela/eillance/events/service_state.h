#pragma once

namespace hela::eillance {

enum class ServiceState : int {
  Unknown = 0,
  Standby,
  Setup,
  Action,
  Failure,
};

}  // namespace hela::eillance
