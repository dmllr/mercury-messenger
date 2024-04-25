#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "opus.h"

#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"
#include "logging/logging.h"
#include "utils/units.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

class AudioStreamSource : public Stream::Source {
private:
  static constexpr auto maxPcmBufferSize = 1000000; // TODO calculate it
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  OpusEncoder* opus_encoder_;

  rtc::binary encoded_data_;
  std::array<unsigned char, maxPcmBufferSize> encoded_buffer_{};
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  uint64_t sample_time_;

public:
  AudioStreamSource() {
    // audio device
    device_config_.capture.format = ma_format_s16;
    device_config_.capture.channels = 1;  // mono
    device_config_.sampleRate = 48000;
    device_config_.pUserData = this;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 frame_count) {
      auto* this_ = static_cast<AudioStreamSource*>(device->pUserData);
      if (this_ != nullptr) {
        this_->EncodeBuffer(frames, frame_count);
      }
    };

    // opus
    auto error = 0;
    opus_encoder_ = opus_encoder_create(device_config_.sampleRate, device_config_.capture.channels, OPUS_APPLICATION_AUDIO, &error);
    assert(error == OPUS_OK);
    assert(opus_encoder_ != nullptr);
  }

  void Start() final {
    auto result = ma_context_init(NULL, 0, NULL, &context_);
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

    start_time_ = std::chrono::steady_clock::now();
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  void LoadNextSample() final {
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = encoded_data_;
    encoded_data_.clear();

    LOGV << "Sending audio sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() final {
    // Stop and uninitialize the audio device
    ma_device_uninit(&audio_device_);

    // Uninitialize the Miniaudio context
    ma_context_uninit(&context_);
  }

  auto GetSampleTime_us() -> uint64_t final {
    return sample_time_;
  }

private:
  void EncodeBuffer(const void* frames, ma_uint32 frame_count) {
    assert(device_config_.capture.format == ma_format_s16);
    auto* pcm_buffer = static_cast<const opus_int16*>(frames);

    if (likely(frame_count > 0)) {
      auto size = opus_encode(
          opus_encoder_, pcm_buffer, frame_count,
          encoded_buffer_.data(), static_cast<opus_int32>(encoded_buffer_.size()));
      if (likely(size > 0)) {
        encoded_data_.insert(
            encoded_data_.end(),
            reinterpret_cast<std::byte*>(&encoded_buffer_[0]),
            reinterpret_cast<std::byte*>(&encoded_buffer_[size]));
      }
    }
  }

};
