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

  uint32_t chunkSize = *reinterpret_cast<uint32_t*>(header + 4);
  uint16_t format = *reinterpret_cast<uint16_t*>(header + 20);
  uint16_t channels = *reinterpret_cast<uint16_t*>(header + 22);
  uint32_t sampleRate = *reinterpret_cast<uint32_t*>(header + 24);
  uint16_t bitsPerSample = *reinterpret_cast<uint16_t*>(header + 34);

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
  std::vector<float> pcmBuffer = hela::eillance::ReadWavFile(wavFile);

  // Encode PCM buffer frame by frame
  std::vector<hela::eillance::Packet> encodedPackets;
  const size_t frameSize = hela::eillance::AudioConfig::numberOfSamples * hela::eillance::AudioConfig::channels;
  for (size_t i = 0; i < pcmBuffer.size(); i += frameSize) {
    const hela::eillance::Packet encodedPacket = encoder.EncodeBuffer(&pcmBuffer[i]);
    if (!encodedPacket.data.empty()) {
      encodedPackets.push_back(encodedPacket);
    } else {
      std::cerr << "Failed to encode audio frame " << (i / frameSize) << std::endl;
      encoder.Revoke();
      return 1;
    }
  }

  // Check if encoding was successful
  if (encodedPackets.empty()) {
    std::cerr << "No audio frames encoded." << std::endl;
    encoder.Revoke();
    return 1;
  }

  // Create the output file path with the .opus extension
  auto wav_path{fs::path(wavFile)};
  auto opusPath = wav_path;
  #if defined(USE_LIBOPUS)
  opusPath = opusPath.replace_filename("recording_libopus.opus");
  #elif defined(USE_LIBFFMPEG)
  opusPath = opusPath.replace_filename("recording_ffmpeg.opus");
  #endif

  // Write encoded audio to Opus file
  auto outFile{std::ofstream(opusPath, std::ios::binary)};
  if (!outFile.is_open()) {
    std::cerr << "Failed to open output file." << std::endl;
    encoder.Revoke();
    return 1;
  }

  for (const auto& packet : encodedPackets) {
    outFile.write(reinterpret_cast<const char*>(packet.data.data()), packet.data.size());
  }

  // Check if writing was successful
  if (!outFile) {
    std::cerr << "Failed to write to output file." << std::endl;
    encoder.Revoke();
    return 1;
  }

  // Close output file
  outFile.close();

  std::cout << "Audio encoding and writing to file successful." << std::endl;

  // Cleanup
  encoder.Revoke();

  return 0;
}
