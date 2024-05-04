#undef MA_NO_ENCODING
#include "miniaudio.h"

#include <iostream>
#include <string>
#include <filesystem>

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  ma_encoder* pEncoder = (ma_encoder*)pDevice->pUserData;
  MA_ASSERT(pEncoder != NULL);

  ma_encoder_write_pcm_frames(pEncoder, pInput, frameCount, NULL);

  (void)pOutput;
}

int main(int argc, char** argv) {
  const auto exePath = std::filesystem::absolute(argv[0]);
  const auto outputDir = exePath.parent_path();
  const auto outputFile = outputDir / "recording.wav";

  ma_encoder encoder;
  ma_encoder_config encoderConfig;
  encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 48000);

  if (ma_encoder_init_file(outputFile.string().c_str(), &encoderConfig, &encoder) != MA_SUCCESS) {
    std::cerr << "Failed to initialize output file.\n";
    return -1;
  }

  auto deviceConfig = ma_device_config_init(ma_device_type_capture);
  deviceConfig.capture.format   = encoder.config.format;
  deviceConfig.capture.channels = encoder.config.channels;
  deviceConfig.sampleRate       = encoder.config.sampleRate;
  deviceConfig.dataCallback     = data_callback;
  deviceConfig.pUserData        = &encoder;

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

  std::cout << "Press Enter to stop recording..." << std::endl;
  getchar();

  ma_device_uninit(&device);
  ma_encoder_uninit(&encoder);

  return 0;
}
