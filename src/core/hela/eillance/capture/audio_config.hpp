#pragma once

#include <miniaudio.h>
#include <opus.h>

namespace hela::eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  using SampleDataType = std::int16_t;
  static constexpr auto maSampleFormat = ma_format_s16;
  static constexpr auto opusEncodeFunction = opus_encode;
  static constexpr auto channels = 2U;
  static constexpr auto sampleDuration = 10ULL;  // milliseconds
  static constexpr auto samplesPerSecond = 1000ULL / sampleDuration;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto frameSize = sampleRate / samplesPerSecond;
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encodedFrameSizeBytes = 4000;  // 4000 is recommended value, however real encoding needs much less memory (~162 bytes)
};

}  // namespace hela::eillance
