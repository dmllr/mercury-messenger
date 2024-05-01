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
    opus_encoder_ = opus_encoder_create(AudioConfig::sampleRate, AudioConfig::channels, OPUS_APPLICATION_AUDIO, &error);
    assert(error == OPUS_OK);
    assert(opus_encoder_ != nullptr);
  }

  void Revoke() {
    opus_encoder_destroy(opus_encoder_);
  }

  Packet EncodeBuffer(Packet raw_buffers) {
    auto* pcm_buffer = reinterpret_cast<const float*>(raw_buffers.data.data());

    const auto MAX_PCM_BUFFER_SIZE = raw_buffers.amount * 480ULL * AudioConfig::channels * sizeof(float);
    std::vector<unsigned char> encoded_buffer(MAX_PCM_BUFFER_SIZE);

    if (likely(raw_buffers.amount > 0)) {
      auto size = opus_encode_float(
          opus_encoder_, pcm_buffer, raw_buffers.amount,
          encoded_buffer.data(), static_cast<opus_int32>(encoded_buffer.size()));
      if (likely(size > 0)) {
        auto result = Packet{
            std::vector(
                reinterpret_cast<std::byte*>(encoded_buffer.data()),
                reinterpret_cast<std::byte*>(std::next(encoded_buffer.data(), size))),
            raw_buffers.amount};
        return result;
      }
    }
    return {};
  }

};

}  // namespace hela::eillance
