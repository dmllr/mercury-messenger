#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <opus.h>

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/time.hpp"
#include "hela/eillance/utils/units.hpp"

namespace hela::eillance {

class AudioEncoder {
  OpusEncoder* opus_encoder_;

public:
  void Engage() {
    // opus
    auto error = 0;
    opus_encoder_ = opus_encoder_create(AudioConfig::sampleRate, AudioConfig::channels, OPUS_APPLICATION_VOIP, &error);
    assert(error == OPUS_OK);
    assert(opus_encoder_ != nullptr);

    opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(AudioConfig::bitrate));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(5));  // 1 to 10
    opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  }

  void Revoke() {
    opus_encoder_destroy(opus_encoder_);
  }

  Packet EncodeBuffer(AudioConfig::sampleDataType* pcm_buffer) {
    auto encoded_buffer{std::array<unsigned char, AudioConfig::encodedFrameSizeBytes>{}};

    auto size = AudioConfig::opusEncodeFunction(
        opus_encoder_, pcm_buffer, AudioConfig::frameSize,
        encoded_buffer.data(), static_cast<opus_int32>(encoded_buffer.size()));
    if (likely(size > 1)) {
      // 1-byte packets should not be transmitted
      return Packet{encoded_buffer.data(), static_cast<uint32_t>(size)};
    }
    return {};
  }

};

}  // namespace hela::eillance
