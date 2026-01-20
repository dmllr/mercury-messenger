#pragma once

#include <cstdint>

namespace eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  using SampleDataType = std::int16_t;
  static constexpr auto sampleDuration = 10ULL;  // milliseconds
  static constexpr auto samplesPerSecond = 1000ULL / sampleDuration;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto frameSize = sampleRate / samplesPerSecond;
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encodedFrameSizeBytes =
      4000;  // 4000 is recommended value, however real encoding needs much less memory (~162 bytes)
  static constexpr auto channels = 2U;
};

}  // namespace eillance
