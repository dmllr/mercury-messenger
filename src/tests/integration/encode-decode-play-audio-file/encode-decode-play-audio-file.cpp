#undef MA_NO_DECODING
#undef MA_NO_GENERATION
#undef MA_NO_RESOURCE_MANAGER
#undef MA_NO_ENGINE

#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <miniaudio.h>
#include <opus.h>

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/capture/audio_encoder_libopus.hpp"

namespace hela::eillance {

std::vector<float> ReadWavFile(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << filename << std::endl;
    exit(1);
  }

  // Read WAV header
  char header[44];
  file.read(header, 44);
  if (!file) {
    std::cerr << "Failed to read WAV header." << std::endl;
    exit(1);
  }

  // Verify WAV format
  if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVEfmt ", 8) != 0) {
    std::cerr << "Invalid WAV format." << std::endl;
    exit(1);
  }

  const auto chunk_size = *reinterpret_cast<uint32_t*>(header + 4);
  const auto format = *reinterpret_cast<uint16_t*>(header + 20);
  const auto channels = *reinterpret_cast<uint16_t*>(header + 22);
  const auto sample_rate = *reinterpret_cast<uint32_t*>(header + 24);
  const auto bits_per_sample = *reinterpret_cast<uint16_t*>(header + 34);

  if (format != 1 && format != 3) {
    std::cerr << "Unsupported WAV format." << std::endl;
    exit(1);
  }

  if (channels != AudioConfig::channels ||
      sample_rate != AudioConfig::sampleRate) {
    std::cerr << "Unsupported audio configuration." << std::endl;
    exit(1);
  }

  if (bits_per_sample != 32 && format == 1) {
    std::cerr << "Unsupported sample resolution." << std::endl;
    exit(1);
  }

  // Read PCM data
  std::vector<float> pcmData((chunk_size - 36) / 4);
  file.read(reinterpret_cast<char*>(pcmData.data()), pcmData.size() * sizeof(float));
  if (!file) {
    std::cerr << "Failed to read PCM data." << std::endl;
    exit(1);
  }

  return pcmData;
}

} // namespace hela::eillance

std::vector<float> pcm_buffer;

// Custom data callback function for miniaudio
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
  // Cast output buffer to float
  auto* pOutputF32 = (float*)pOutput;
  auto* pcm = pcm_buffer.data();

  // Fill output buffer with silence
  for (ma_uint32 i = 0; i < frameCount * pDevice->playback.channels; ++i) {
    *pOutputF32++ = *pcm++;
  }
}

int main() {
  // Initialize AudioEncoder
  hela::eillance::AudioEncoder encoder{};
  encoder.Engage();

  // Read WAV file
  const auto* wav_file = "C:\\Users\\DenisMiller\\dev\\hela-surveillance\\src\\tests\\integration\\encode-decode-play-audio-file\\recording2ch.wav";
  pcm_buffer = hela::eillance::ReadWavFile(wav_file);

  // Initialize Opus decoder
  int error;
  OpusDecoder* decoder = opus_decoder_create(hela::eillance::AudioConfig::sampleRate, hela::eillance::AudioConfig::channels, &error);
  if (error != OPUS_OK) {
    std::cerr << "Failed to initialize Opus decoder." << std::endl;
    return 1;
  }

  // Setup miniaudio
  ma_result result;
  ma_device device;
  ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format   = ma_format_f32;
  deviceConfig.playback.channels = hela::eillance::AudioConfig::channels;
  deviceConfig.sampleRate        = hela::eillance::AudioConfig::sampleRate;
  deviceConfig.dataCallback      = data_callback; // Set the custom data callback

  result = ma_device_init(NULL, &deviceConfig, &device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize playback device." << std::endl;
    return 1;
  }

  // Start playback
  result = ma_device_start(&device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to start playback." << std::endl;
    ma_device_uninit(&device);
    opus_decoder_destroy(decoder);
    return 1;
  }

//  // Decode encoded packets and play
//  size_t frame_size = hela::eillance::AudioConfig::frameSize;
//  for (size_t i = 0; i < pcm_buffer.size(); i += frame_size) {
////    // Encode PCM frame
////    const auto encoded_packet = encoder.EncodeBuffer(&pcm_buffer[i]);
////    if (encoded_packet.empty()) {
////      // Failed to encode
////      continue;
////    }
////
////    // Decode Opus packet
////    int decoded_samples = opus_decode_float(decoder, encoded_packet.dataAsUChars(), encoded_packet.size(), pcm_buffer.data(), frame_size, 0);
////    if (decoded_samples < 0) {
////      std::cerr << "Failed to decode Opus packet." << std::endl;
////      continue;
////    }
//  }

  // Wait a while to let the audio play
  ma_sleep(2000); // Adjust time as necessary

  // Stop playback
  ma_device_stop(&device);

  // Cleanup
  ma_device_uninit(&device);
  encoder.Revoke();
  opus_decoder_destroy(decoder);

  return 0;
}
