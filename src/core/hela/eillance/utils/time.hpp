#pragma once

#include <chrono>
#include <cstdint>

class Time {
public:
  // NOLINTNEXTLINE(*-identifier-length)
  static constexpr auto uS = 1000000ULL;

  using DefaultClock = std::chrono::steady_clock;

  using Unit = std::uint64_t;

  static Unit Now() {
    auto current_time = DefaultClock::now();
    auto since_epoch = current_time.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
  }
};
