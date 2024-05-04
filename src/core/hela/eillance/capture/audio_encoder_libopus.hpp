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
  }

  void Revoke() {
    opus_encoder_destroy(opus_encoder_);
  }

  Packet EncodeBuffer(float* pcm_buffer) {
    std::vector<unsigned char> encoded_buffer(AudioConfig::encodedFrameSizeBytes);

    auto size = opus_encode_float(
        opus_encoder_, pcm_buffer, AudioConfig::frameSize,
        encoded_buffer.data(), static_cast<opus_int32>(encoded_buffer.size()));
    if (likely(size > 0)) {
      return Packet{encoded_buffer.data(), static_cast<uint32_t>(size)};
    }
    return {};
  }

};

}  // namespace hela::eillance
