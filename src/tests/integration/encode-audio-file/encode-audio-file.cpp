#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <filesystem>

#include "hela/eillance/capture/audio_config.hpp"
#if defined(USE_LIBOPUS)
#include "hela/eillance/capture/audio_encoder_libopus.hpp"
#elif defined(USE_LIBFFMPEG)
#include "hela/eillance/capture/audio_encoder_libffmpeg.hpp"
#else
#error Define USE_LIBOPUS or USE_LIBFFMPEG
#endif
namespace fs = std::filesystem;

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

  const auto chunkSize = *reinterpret_cast<uint32_t*>(header + 4);
  const auto format = *reinterpret_cast<uint16_t*>(header + 20);
  const auto channels = *reinterpret_cast<uint16_t*>(header + 22);
  const auto sampleRate = *reinterpret_cast<uint32_t*>(header + 24);
  const auto bitsPerSample = *reinterpret_cast<uint16_t*>(header + 34);

  if (format != 1 && format != 3) {
    std::cerr << "Unsupported WAV format." << std::endl;
    exit(1);
  }

  if (channels != AudioConfig::channels ||
      sampleRate != AudioConfig::sampleRate) {
    std::cerr << "Unsupported audio configuration." << std::endl;
    exit(1);
  }

  if (bitsPerSample != 32 && format == 1) {
    std::cerr << "Unsupported sample resolution." << std::endl;
    exit(1);
  }

  // Read PCM data
  std::vector<float> pcmData((chunkSize - 36) / 4);
  file.read(reinterpret_cast<char*>(pcmData.data()), pcmData.size() * sizeof(float));
  if (!file) {
    std::cerr << "Failed to read PCM data." << std::endl;
    exit(1);
  }

  return pcmData;
}

} // namespace hela::eillance

int main() {
  // Initialize AudioEncoder
  hela::eillance::AudioEncoder encoder{};
  encoder.Engage();

  // Read WAV file
  const auto* wavFile = "C:\\Users\\DenisMiller\\dev\\hela-surveillance\\src\\tests\\integration\\encode-audio-file\\recording.wav";
  std::vector<float> pcm_buffer = hela::eillance::ReadWavFile(wavFile);

  // Encode PCM buffer frame by frame
  std::vector<hela::eillance::Packet> encoded_packets;
  const size_t frame_size = hela::eillance::AudioConfig::numberOfSamples * hela::eillance::AudioConfig::channels;
  for (size_t i = 0; i < pcm_buffer.size(); i += frame_size) {
    const hela::eillance::Packet encodedPacket = encoder.EncodeBuffer(&pcm_buffer[i]);
    if (!encodedPacket.data.empty()) {
      encoded_packets.push_back(encodedPacket);
    } else {
      std::cerr << "Failed to encode audio frame " << (i / frame_size) << std::endl;
      encoder.Revoke();
      return 1;
    }
  }

  // Check if encoding was successful
  if (encoded_packets.empty()) {
    std::cerr << "No audio frames encoded." << std::endl;
    encoder.Revoke();
    return 1;
  }

  // Create the output file path with the .opus extension
  auto wav_path{fs::path(wavFile)};
  auto opus_path = wav_path;
  #if defined(USE_LIBOPUS)
  opus_path = opus_path.replace_filename("recording_libopus.opus");
  #elif defined(USE_LIBFFMPEG)
  opus_path = opus_path.replace_filename("recording_ffmpeg.opus");
  #endif

  // Write encoded audio to Opus file
  auto out_file{std::ofstream(opus_path, std::ios::binary)};
  if (!out_file.is_open()) {
    std::cerr << "Failed to open output file." << std::endl;
    encoder.Revoke();
    return 1;
  }

  for (const auto& packet : encoded_packets) {
    out_file.write(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
  }

  // Check if writing was successful
  if (!out_file) {
    std::cerr << "Failed to write to output file." << std::endl;
    encoder.Revoke();
    return 1;
  }

  // Close output file
  out_file.close();

  std::cout << "Audio encoding successful." << std::endl;

  // Cleanup
  encoder.Revoke();

  return 0;
}
