#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <miniaudio.h>
#include <rtc/common.hpp>

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/capture/audio_encoder.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/units.hpp"

namespace hela::eillance {

class AudioCapture {
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  std::array<AudioConfig::SampleDataType, AudioConfig::frameSize> pcm_buffer_{};

  AudioEncoder encoder_{};

  std::mutex data_access_mutex_{};

public:
  AudioCapture() {
    // audio device
    device_config_.capture.format = AudioConfig::maSampleFormat;
    device_config_.capture.channels = AudioConfig::channels;
    device_config_.sampleRate = AudioConfig::sampleRate;
    device_config_.pUserData = this;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 nb_samples) {
      assert(nb_samples == AudioConfig::frameSize);
      auto* self = static_cast<AudioCapture*>(device->pUserData);
      if (self == nullptr) {
        return;
      }
      const auto* buffer = static_cast<const AudioConfig::SampleDataType*>(frames);
      {
        auto lock = std::lock_guard<decltype(data_access_mutex_)>(self->data_access_mutex_);
        std::copy(
            buffer,
            std::next(buffer, AudioConfig::frameSize),
            self->pcm_buffer_.begin());
      }
    };
  }

  void Engage() {
    encoder_.Engage();

    auto result = ma_context_init(nullptr, 0, nullptr, &context_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to initialize Miniaudio context";
      return;
    }

    // Open the audio device
    result = ma_device_init(&context_, &device_config_, &audio_device_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to open audio device";
      ma_context_uninit(&context_);
      return;
    }

    // Start the audio device
    result = ma_device_start(&audio_device_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to start audio device";
      ma_device_uninit(&audio_device_);
      ma_context_uninit(&context_);
      return;
    }
  }

  void Revoke() {
    encoder_.Revoke();
  }

  Packet FetchBuffers() {
    auto lock = std::lock_guard<decltype(data_access_mutex_)>(data_access_mutex_);
    return encoder_.EncodeBuffer(pcm_buffer_.data());
  }

};

}  // namespace hela::eillance
