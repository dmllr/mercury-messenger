#pragma once

#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  static constexpr auto format = ma_format_f32;
  static constexpr auto channels = 2U;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto frameSize = (sampleRate / 100);  // 10ms (1/100s) per frame
  static constexpr auto frameSizeBytes = frameSize * sizeof(float);
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encodedFrameSizeBytes = 4000; //2 * (bitrate / 100);  // 4000 is recommended value // 10ms (1/100s) per frame, twice than estimated
};

}  // namespace hela::eillance
