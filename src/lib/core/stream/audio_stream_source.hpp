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
  static constexpr auto MAX_PCM_BUFFER_SIZE = 1000000;
  ma_context context;
  ma_device audio_device;
  ma_device_config device_config = ma_device_config_init(ma_device_type_capture);

  rtc::binary encoded_data_;
  std::array<unsigned char, MAX_PCM_BUFFER_SIZE> encoded_buffer_{};
  std::chrono::time_point<std::chrono::steady_clock> start_time;

  OpusEncoder* opus_encoder;

public:
  AudioStreamSource() {
    // audio device
    device_config.capture.format = ma_format_s16;
    device_config.capture.channels = 1;  // mono
    device_config.sampleRate = 44100;
    device_config.pUserData = this;
    device_config.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 frame_count) {
      auto* this_ = static_cast<AudioStreamSource*>(device->pUserData);
      if (this_ != nullptr) {
        this_->EncodeBuffer(frames, frame_count);
      }
    };

    // opus
    auto error = 0;
    opus_encoder = opus_encoder_create(device_config.sampleRate, device_config.capture.channels, OPUS_APPLICATION_AUDIO, &error);
    assert(error == OPUS_OK);
    assert(opus_encoder != nullptr);
  }

  void Start() override {
    ma_result result;
    result = ma_context_init(NULL, 0, NULL, &context);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to initialize Miniaudio context";
      return;
    }

    // Open the audio device
    result = ma_device_init(&context, &device_config, &audio_device);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to open audio device";
      ma_context_uninit(&context);
      return;
    }

    // Start the audio device
    result = ma_device_start(&audio_device);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to start audio device";
      ma_device_uninit(&audio_device);
      ma_context_uninit(&context);
      return;
    }
  }

  void LoadNextSample() override {
  }

  auto GetSample() -> rtc::binary override {
    auto sample = encoded_data_;
    encoded_data_.clear();

    LOGV << "Sending sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() override {
    // Stop and uninitialize the audio device
    ma_device_uninit(&audio_device);

    // Uninitialize the Miniaudio context
    ma_context_uninit(&context);
  }

  auto GetSampleTime_us() -> uint64_t override {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
  }

private:
  void EncodeBuffer(const void* frames, ma_uint32 frame_count) {
    assert(device_config.capture.format != ma_format_s16);
    auto* pcm_buffer = static_cast<const opus_int16*>(frames);

    if (likely(frame_count > 0)) {
      auto size = opus_encode(
          opus_encoder, pcm_buffer, frame_count,
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
