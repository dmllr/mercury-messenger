#pragma once

#include "miniaudio.h"

namespace hela::eillance {

struct AudioConfig {
  static constexpr auto format = ma_format_f32;
  static constexpr auto channels = 2;
  static constexpr auto sampleRate = 48000;
  static constexpr auto frameSize = sampleRate / 50; // sample rate * Frame size (0.02s, i.e. 1/50s);
};

}  // namespace hela::eillance
