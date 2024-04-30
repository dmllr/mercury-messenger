#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <rtc/common.hpp>

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/capture/audio_encoder.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/units.hpp"

#undef max

namespace hela::eillance {

class AudioCapture {
private:
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  AudioEncoder encoder_;

public:
  AudioCapture() {
    // audio device
    device_config_.capture.format = AudioConfig::format;
    device_config_.capture.channels = AudioConfig::channels;
    device_config_.sampleRate = AudioConfig::sampleRate;
    device_config_.pUserData = &encoder_;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 nb_samples) {
      auto* encoder = static_cast<AudioEncoder*>(device->pUserData);
      if (encoder != nullptr) {
        encoder->EncodeBuffer(frames, nb_samples);
        // TODO instead of instant encoding, accumulate buffers
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

  void ProcessCapturedBuffers() {
  }

  void SetDataCallback(DataCallback callback) {
    encoder_.SetDataCallback(std::move(callback));
  }
};

}  // namespace hela::eillance
