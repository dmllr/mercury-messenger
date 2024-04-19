#pragma once

enum class ServiceState : int {
  Unknown = 0,
  Standby,
  Setup,
  Action,
  Failure,
};
