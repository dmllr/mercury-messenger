#pragma once

#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  static constexpr auto format = ma_format_f32;
  static constexpr auto channels = 2U;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto numberOfSamples = sampleRate / 50;  // 20ms per frame
  static constexpr auto frameSize = numberOfSamples * channels;
  static constexpr auto frameSizeBytes = frameSize * sizeof(float);
  static constexpr auto encodedBitrate = 65536ULL;
  static constexpr auto encodedFrameSizeBytes = 8 + (encodedBitrate / 50);  // 20ms per frame
};

}  // namespace hela::eillance
