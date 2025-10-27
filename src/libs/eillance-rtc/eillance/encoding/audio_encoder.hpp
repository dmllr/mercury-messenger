#pragma once

#include <array>
#include <cstdint>

#include <opus.h>

#include "eillance/capture/audio_config.hpp"
#include "eillance/utils/likely.h"
#include "eillance/utils/packet.hpp"

namespace eillance {

class AudioEncoderCli {
  OpusEncoder* opus_encoder_{nullptr};

  // A complexity of 5 (1 to 10) provides a good balance between quality and performance for VOIP.
  static constexpr auto kOpusComplexity = 5;

  // Implementation-specific configuration
  static constexpr auto opus_encode_function = opus_encode;

public:
  void Engage() {
    // opus
    auto error = 0;
    opus_encoder_ = opus_encoder_create(AudioConfig::sample_rate, AudioConfig::channels, OPUS_APPLICATION_VOIP, &error);
    assert(error == OPUS_OK);
    assert(opus_encoder_ != nullptr);

    opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(AudioConfig::bitrate));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(kOpusComplexity));
    opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  }

  void Revoke() const {
    if (opus_encoder_) { opus_encoder_destroy(opus_encoder_); }
  }

  auto EncodeBuffer(const AudioConfig::SampleDataType* pcm_buffer) const -> Packet {
    auto encoded_buffer{std::array<unsigned char, AudioConfig::encoded_frame_size_bytes>{}};

    const auto size = opus_encode_function(opus_encoder_, pcm_buffer, AudioConfig::frame_size, encoded_buffer.data(), static_cast<opus_int32>(encoded_buffer.size()));
    if (likely(size > 1)) {
      // 1-byte packets should not be transmitted
      return Packet{encoded_buffer.data(), static_cast<uint32_t>(size)};
    }
    return {};
  }
};

}  // namespace eillance
