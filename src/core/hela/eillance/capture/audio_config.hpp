#pragma once

#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  static constexpr auto format = ma_format_f32;
  static constexpr auto channels = 2U;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto numberOfSamples = sampleRate / 50;
  static constexpr auto sampleSize = numberOfSamples * AudioConfig::channels * sizeof(float);
};

}  // namespace hela::eillance
