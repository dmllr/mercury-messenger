#pragma once

enum class SignallingState : int {
  Ready = 0,
  Closed,
  Failure,
  Unexpected,
};
