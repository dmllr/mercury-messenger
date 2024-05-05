#pragma once

#include <libavutil/samplefmt.h>
#include <miniaudio.h>

namespace hela::eillance {

struct AudioConfig {
  // This audio config assumes a sample duration to be 10ms.
  using sampleDataType = float;
  static constexpr auto maSampleFormat = ma_format_f32;
  static constexpr auto avSampleFormat = AV_SAMPLE_FMT_FLTP;
  static constexpr auto channels = 2U;
  static constexpr auto sampleRate = 48000ULL;
  static constexpr auto frameSize = (sampleRate / 100);  // 10ms (1/100s) per frame
  static constexpr auto frameSizeBytes = frameSize * sizeof(sampleDataType);
  static constexpr auto bitrate = 64000ULL;
  static constexpr auto encodedFrameSizeBytes = 4000; //2 * (bitrate / 100);  // 4000 is recommended value // 10ms (1/100s) per frame, twice than estimated
};

}  // namespace hela::eillance
