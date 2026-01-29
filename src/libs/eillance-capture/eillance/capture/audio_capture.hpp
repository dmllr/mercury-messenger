#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <miniaudio.h>

#include "eillance/capture/audio_config.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/encoding/audio_encoder.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/logging/telemetry.hpp"
#include "eillance/utils/circular_buffer.hpp"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"

namespace eillance {

class AudioCaptureCli {
  ma_context context_{};
  ma_device audioDevice_{};
  ma_device_config deviceConfig_{ma_device_config_init(ma_device_type_capture)};

  AudioEncoderCli encoder_{};

  // buffer last 1 second of encoded data
  CircularBuffer<Packet, AudioConfig::samplesPerSecond> encoded_packets_;

  // Implementation-specific configuration
  static constexpr auto maSampleFormat = ma_format_s16;

  TELEMETRY(Time::Unit last_fps_milestone{}; std::uint32_t fps_counter{}; std::uint32_t loadCounter_{};)

 public:
  AudioCaptureCli() {
    // audio device
    deviceConfig_.capture.format = maSampleFormat;
    deviceConfig_.capture.channels = AudioConfig::channels;
    deviceConfig_.sampleRate = AudioConfig::sampleRate;
    deviceConfig_.pUserData = this;
    deviceConfig_.dataCallback = [](ma_device* device, void*, void const* frames, ma_uint32 const nb_samples) {
      assert(nb_samples == AudioConfig::frameSize);
      auto* self = static_cast<AudioCaptureCli*>(device->pUserData);
      if (self == nullptr) {
        return;
      }

      auto encoded_packet = self->encoder_.EncodeBuffer(static_cast<AudioConfig::SampleDataType const*>(frames));
      self->encoded_packets_.Push(std::move(encoded_packet));

      TELEMETRY(if (self->fps_counter % (telemetry_interval * AudioConfig::samplesPerSecond) == 0) {
        auto const elapsed = (Time::Now() - self->last_fps_milestone) / Time::uS;
        auto const fetch_fps = static_cast<double>(self->loadCounter_) / elapsed;
        self->loadCounter_ = 0;

        auto const capture_fps = static_cast<double>(self->fps_counter) / elapsed;
        self->last_fps_milestone = Time::Now();
        self->fps_counter = 0;

        LGI << "Audio capture rate FPS: " << std::setprecision(2) << std::fixed << capture_fps
            << ", RTC request rate FPS:" << fetch_fps;
      } self->fps_counter++;)
    };
  }

  void Configure(Config const& /*config*/) {
  }

  void Engage() {
    encoder_.Engage();

    if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
      LGE << "Failed to initialize Miniaudio context";
      return;
    }
    if (ma_device_init(&context_, &deviceConfig_, &audioDevice_) != MA_SUCCESS) {
      LGE << "Failed to open audio device";
      ma_context_uninit(&context_);
      return;
    }
    if (ma_device_start(&audioDevice_) != MA_SUCCESS) {
      LGE << "Failed to start audio device";
      ma_device_uninit(&audioDevice_);
      ma_context_uninit(&context_);
    }
  }

  void Revoke() {
    ma_device_stop(&audioDevice_);
    ma_device_uninit(&audioDevice_);
    ma_context_uninit(&context_);
    encoder_.Revoke();
  }

  auto FetchBuffers() -> Packet {
    TELEMETRY(loadCounter_++;)
    Packet combined_packet;
    while (!encoded_packets_.IsEmpty()) {
      auto packet = encoded_packets_.Pop();
      combined_packet.append(packet);
    }
    return combined_packet;
  }

  static auto GetSampleDuration() -> Time::Unit {
    return AudioConfig::sampleDuration * Time::uS;
  }
};

}  // namespace eillance
