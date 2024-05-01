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

namespace hela::eillance {

class AudioCapture {
private:
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  Packet raw_buffers_;

  AudioEncoder encoder_;

  std::mutex data_access_mutex_;

public:
  AudioCapture() {
    // audio device
    device_config_.capture.format = AudioConfig::format;
    device_config_.capture.channels = AudioConfig::channels;
    device_config_.sampleRate = AudioConfig::sampleRate;
    device_config_.pUserData = this;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 nb_samples) {
      auto* self = static_cast<AudioCapture*>(device->pUserData);
      if (self == nullptr) {
        return;;
      }
      const auto* buffer = static_cast<const std::byte*>(frames);
      const auto size = nb_samples * AudioConfig::channels * sizeof(float);
      {
        auto lock = std::lock_guard<decltype(data_access_mutex_)>(self->data_access_mutex_);
        self->raw_buffers_.data.insert(
            self->raw_buffers_.data.end(),
            buffer,
            std::next(buffer, size));  // NOLINT(*-narrowing-conversions)
        self->raw_buffers_.amount += nb_samples;
        if (0) {
          std::cout << "+: size = " << self->raw_buffers_.data.size() << ", samples = " << self->raw_buffers_.amount << std::endl;
        }
      }
    };
  }

  void Engage() {
    raw_buffers_.Clear();

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
    data_access_mutex_.lock();
    auto buffers = raw_buffers_;
    raw_buffers_.Clear();
    if (0) {
      std::cout << "-: size = " << buffers.data.size() << ", samples = " << buffers.amount << ", raw: size = " << raw_buffers_.data.size() << ", samples = " << raw_buffers_.amount << std::endl;
    }
    data_access_mutex_.unlock();
    if (buffers.amount == 0) {
      return {};
    }
    return encoder_.EncodeBuffer(std::move(buffers));
  }

};

}  // namespace hela::eillance
