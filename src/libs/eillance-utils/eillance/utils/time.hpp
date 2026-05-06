#pragma once

#include <chrono>
#include <cstdint>

class Time {
 public:
  // NOLINTBEGIN(*-identifier-length)
  // rationale: `Time::mS` is self-explanatory
  static constexpr auto mS = 1000ULL;
  static constexpr auto uS = 1000000ULL;
  // NOLINTEND(*-identifier-length)

  using DefaultClock = std::chrono::system_clock;
  using SteadyClock = std::chrono::steady_clock;

  using Unit = std::uint64_t;

  static auto Now() -> Unit {
    auto const current_time = DefaultClock::now();
    auto const since_epoch = current_time.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
  }

  static auto SteadyNow() -> Unit {
    auto const current_time = SteadyClock::now();
    auto const since_epoch = current_time.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(since_epoch).count();
  }
};
