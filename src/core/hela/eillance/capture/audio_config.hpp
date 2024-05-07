#pragma once

#include <opus.h>
#include <libavutil/samplefmt.h>
#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  using SampleDataType = std::int16_t;
  static constexpr auto maSampleFormat = ma_format_s16;
  static constexpr auto opusEncodeFunction = opus_encode;
  static constexpr auto channels = 2U;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto frameSize = (sampleRate / 100);  // 10ms (1/100s) per frame
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encodedFrameSizeBytes = 4000; // 4000 is recommended value // //2 * (bitrate / 100); // 10ms (1/100s) per frame, twice than estimated
};

}  // namespace hela::eillance
