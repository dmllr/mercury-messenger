#pragma once

#include <cstdint>

namespace eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  using SampleDataType = std::int16_t;
  static constexpr auto channels = 2U;
  static constexpr auto sample_duration = 10ULL;  // milliseconds
  static constexpr auto samples_per_second = 1000ULL / sample_duration;
  static constexpr auto sample_rate = 48000ULL;
  static constexpr auto frame_size = sample_rate / samples_per_second;
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encoded_frame_size_bytes = 4000;  // 4000 is recommended value, however real encoding needs much less memory (~162 bytes)
};

}  // namespace eillance
