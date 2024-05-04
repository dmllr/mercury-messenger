#undef MA_NO_ENCODING
#include "miniaudio.h"

#include <iostream>
#include <string>
#include <filesystem>

void DataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
  auto* pEncoder = (ma_encoder*)pDevice->pUserData;
  MA_ASSERT(pEncoder != nullptr);

  ma_encoder_write_pcm_frames(pEncoder, pInput, frameCount, nullptr);

  (void)pOutput;
}

int main(int  /*argc*/, char** argv) {
  const auto exe_path = std::filesystem::absolute(argv[0]);
  const auto output_dir = exe_path.parent_path();
  const auto output_file = output_dir / "recording.wav";

  ma_encoder encoder;
  ma_encoder_config encoder_config;
  encoder_config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 48000);

  if (ma_encoder_init_file(output_file.string().c_str(), &encoder_config, &encoder) != MA_SUCCESS) {
    std::cerr << "Failed to initialize output file.\n";
    return -1;
  }

  auto device_config = ma_device_config_init(ma_device_type_capture);
  device_config.capture.format   = encoder.config.format;
  device_config.capture.channels = encoder.config.channels;
  device_config.sampleRate       = encoder.config.sampleRate;
  device_config.dataCallback     = DataCallback;
  device_config.pUserData        = &encoder;

  ma_device device;
  auto result = ma_device_init(NULL, &device_config, &device);
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
