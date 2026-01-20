#pragma once

#include <chrono>
#include <cstdint>

class Time {
public:
  // NOLINTBEGIN(*-identifier-length)
  static constexpr auto mS = 1000ULL;
  static constexpr auto uS = 1000000ULL;
  // NOLINTEND(*-identifier-length)

  using DefaultClock = std::chrono::steady_clock;

  using Unit = std::uint64_t;

  static Unit Now() {
    const auto current_time = DefaultClock::now();
    const auto since_epoch = current_time.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
  }
};
