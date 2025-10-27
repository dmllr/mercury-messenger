#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <miniaudio.h>

#include "eillance/capture/audio_config.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/encoding/audio_encoder.hpp"
#include "eillance/logging/logging.h"
#include "eillance/logging/telemetry.h"
#include "eillance/utils/circular_buffer.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"

namespace eillance {

class AudioCaptureCli {
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  AudioEncoderCli encoder_{};

  // buffer last 1 second of encoded data
  CircularBuffer<Packet, AudioConfig::samples_per_second> encoded_packets_;

  // Implementation-specific configuration
  static constexpr auto ma_sample_format = ma_format_s16;

  TELEMETRY(Time::Unit last_fps_milestone{}; std::uint32_t fps_counter{}; std::uint32_t load_counter_{};)

public:
  AudioCaptureCli() {
    // audio device
    device_config_.capture.format = ma_sample_format;
    device_config_.capture.channels = AudioConfig::channels;
    device_config_.sampleRate = AudioConfig::sample_rate;
    device_config_.pUserData = this;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, const ma_uint32 nb_samples) {
      assert(nb_samples == AudioConfig::frame_size);
      auto* self = static_cast<AudioCaptureCli*>(device->pUserData);
      if (self == nullptr) { return; }

      auto encoded_packet = self->encoder_.EncodeBuffer(static_cast<const AudioConfig::SampleDataType*>(frames));
      self->encoded_packets_.push(std::move(encoded_packet));

      TELEMETRY(if (self->fps_counter % (telemetry_interval * AudioConfig::samples_per_second) == 0) {
        const auto elapsed = (Time::Now() - self->last_fps_milestone) / Time::uS;
        const auto fetch_fps = static_cast<double>(self->load_counter_) / elapsed;
        self->load_counter_ = 0;

        const auto capture_fps = static_cast<double>(self->fps_counter) / elapsed;
        self->last_fps_milestone = Time::Now();
        self->fps_counter = 0;

        LOGI << "Audio capture rate FPS: " << std::setprecision(2) << std::fixed << capture_fps << ", RTC request rate FPS:" << fetch_fps;
      } self->fps_counter++;)
    };
  }

  void Configure(const Config& /*config*/) {}

  void Engage() {
    encoder_.Engage();

    if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
      LOGE << "Failed to initialize Miniaudio context";
      return;
    }
    if (ma_device_init(&context_, &device_config_, &audio_device_) != MA_SUCCESS) {
      LOGE << "Failed to open audio device";
      ma_context_uninit(&context_);
      return;
    }
    if (ma_device_start(&audio_device_) != MA_SUCCESS) {
      LOGE << "Failed to start audio device";
      ma_device_uninit(&audio_device_);
      ma_context_uninit(&context_);
    }
  }

  void Revoke() {
    ma_device_stop(&audio_device_);
    ma_device_uninit(&audio_device_);
    ma_context_uninit(&context_);
    encoder_.Revoke();
  }

  auto FetchBuffers() -> Packet {
    TELEMETRY(load_counter_++;)
    Packet combined_packet;
    while (!encoded_packets_.isEmpty()) {
      auto packet = encoded_packets_.pop();
      combined_packet.append(packet);
    }
    return combined_packet;
  }

  static auto GetSampleDuration() -> Time::Unit { return AudioConfig::sample_duration * Time::uS; }
};

}  // namespace eillance
