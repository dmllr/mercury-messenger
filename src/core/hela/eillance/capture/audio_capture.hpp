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
#include "hela/eillance/common/circular_buffer.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/logging/telemetry.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

class AudioCapture {
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  AudioEncoder encoder_{};

  // buffer last 1 second of encoded data
  CircularBuffer<Packet, AudioConfig::samplesPerSecond> encoded_packets_;

  TELEMETRY(
      Time::Unit last_fps_milestone;
      std::uint32_t fps_counter;
      std::uint32_t load_counter;)

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

      const auto encoded_packet = self->encoder_.EncodeBuffer(static_cast<const AudioConfig::SampleDataType*>(frames));
      self->encoded_packets_.push(std::move(encoded_packet));

      TELEMETRY(
          if (self->fps_counter % (TelemetryInterval * (AudioConfig::samplesPerSecond / AudioConfig::sampleDuration)) == 0) {
            auto elapsed = (Time::Now() - self->last_fps_milestone) / Time::uS;
            auto fetch_fps = static_cast<double>(self->load_counter) / elapsed;
            self->load_counter = 0;

            auto capture_fps = static_cast<double>(self->fps_counter) / elapsed;
            self->last_fps_milestone = Time::Now();
            self->fps_counter = 0;

            LOGI << "Audio capture rate FPS: " << std::setprecision(2) << std::fixed << capture_fps << ", RTC request rate FPS:" << fetch_fps;
          } self->fps_counter++;)
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
    TELEMETRY(
        load_counter++;)
    Packet combined_packet;
    while (!encoded_packets_.isEmpty()) {
      Packet packet = encoded_packets_.pop();
      combined_packet.append(packet);
    }
    return combined_packet;
  }

  static Time::Unit GetSampleDuration() {
    return AudioConfig::sampleDuration * Time::uS;
  }
};

}  // namespace hela::eillance
