#pragma once

#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  static constexpr auto format = ma_format_f32;
  static constexpr auto channels = 2;
  static constexpr auto sampleRate = 48000;
};

}  // namespace hela::eillance
