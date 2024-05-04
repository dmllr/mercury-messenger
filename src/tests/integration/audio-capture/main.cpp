#include "miniaudio.h"

#include <chrono>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>

void data_callback(ma_device*, void*, const void* pInput, ma_uint32 frameCount) {
  std::cout << '.' << std::flush;

  assert(frameCount > 0);

  float sum{0};
  auto frames = static_cast<const float*>(pInput);
  for (int i = 0; i < frameCount; ++i) {
    sum += frames[i];
  }
  std::cout << sum;
}

int main(int argc, char** argv) {
  const auto exePath = std::filesystem::absolute(argv[0]);
  const auto outputDir = exePath.parent_path();
  const auto outputFile = outputDir / "recording.wav";

  auto deviceConfig = ma_device_config_init(ma_device_type_capture);
  deviceConfig.capture.format   = ma_format_f32;
  deviceConfig.capture.channels = 2;
  deviceConfig.sampleRate       = 48000;
  deviceConfig.dataCallback     = data_callback;

  ma_device device;
  auto result = ma_device_init(NULL, &deviceConfig, &device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize capture device." << std::endl;
    return -2;
  }

  result = ma_device_start(&device);
  if (result != MA_SUCCESS) {
    ma_device_uninit(&device);
    std::cerr << "Failed to start device." << std::endl;
    return -3;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  ma_device_uninit(&device);

  return 0;
}
