#pragma once

#include <chrono>
#include <cstdint>

class Time {
public:
  using DefaultClock = std::chrono::high_resolution_clock;

  static std::uint64_t Now() {
    auto current_time = DefaultClock::now();
    auto since_epoch = current_time.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
  }
};
