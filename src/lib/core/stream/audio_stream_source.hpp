#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"
#include "logging/logging.h"
#include "utils/units.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

class AudioStreamSource : public Stream::Source {
private:
  ma_context context;
  ma_device device;

  rtc::binary encoded_data;
  std::chrono::time_point<std::chrono::steady_clock> start_time;

public:
  AudioStreamSource() = default;

  void Start() override {
    ma_result result;
    result = ma_context_init(NULL, 0, NULL, &context);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to initialize Miniaudio context";
      return;
    }

    // Configure the audio device
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = 1;  // mono
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = [](ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {

    };

    // Open the audio device
    result = ma_device_init(&context, &deviceConfig, &device);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to open audio device";
      ma_context_uninit(&context);
      return;
    }

    // Start the audio device
    result = ma_device_start(&device);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to start audio device";
      ma_device_uninit(&device);
      ma_context_uninit(&context);
      return;
    }
  }

  void LoadNextSample() override {
  }

  auto GetSample() -> rtc::binary override {
    auto sample = encoded_data;
    encoded_data.clear();

    LOGV << "Sending sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() override {
    // Stop and uninitialize the audio device
    ma_device_uninit(&device);

    // Uninitialize the Miniaudio context
    ma_context_uninit(&context);
  }

  auto GetSampleTime_us() -> uint64_t override {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
  }

};
