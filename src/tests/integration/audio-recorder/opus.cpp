#include <iostream>
#include <miniaudio.h>
#include <opus.h>
#include <fstream>
#include <cstdint>

#define SAMPLE_RATE     48000
#define CHANNELS        1
#define FRAME_SIZE      960
#define OPUS_BITRATE    64000

std::ofstream outputFile;

void write_ogg_header() {
  // OggS header
  const uint8_t ogg_header[] = {
      'O', 'g', 'g', 'S', // Magic signature
      0x00,               // Version
      0x02,               // Header type (beginning of stream)
      0x00, 0x00, 0x00, 0x00, // Granule position
      0xFF, 0xFF, 0xFF, 0xFF, // Serial number
      0xFF, 0xFF, 0xFF, 0xFF, // Page sequence number
      0x01                // Checksum
  };

  outputFile.write(reinterpret_cast<const char*>(ogg_header), sizeof(ogg_header));
}

void write_opus_header() {
  const char opus_header[] = {
      'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', // Magic signature
      0x01,                                   // Version number
      CHANNELS,                               // Channel count
      0, 0,                                   // Pre-skip (little endian)
      SAMPLE_RATE & 0xFF, (SAMPLE_RATE >> 8) & 0xFF, (SAMPLE_RATE >> 16) & 0xFF, (SAMPLE_RATE >> 24) & 0xFF, // Input sample rate (little endian)
      0, 0,                                   // Output gain (little endian, signed Q7.8 in dB)
      0                                       // Channel mapping family (0 = one stream: mono or L,R stereo)
  };

  outputFile.write(reinterpret_cast<const char*>(opus_header), sizeof(opus_header));
}

void write_ogg_page(const void* data, size_t size) {
  // Ogg page header
  constexpr uint32_t header_size = 27;
  const uint32_t body_size = static_cast<uint32_t>(size);
  const uint8_t header[header_size] = {
      0x00, // OggS capture pattern
      0x02, // OggS version
      0x00, // OggS header type (continued packet)
      static_cast<uint8_t>(body_size & 0xFF), // OggS body size
      static_cast<uint8_t>((body_size >> 8) & 0xFF),
      static_cast<uint8_t>((body_size >> 16) & 0xFF),
      static_cast<uint8_t>((body_size >> 24) & 0xFF),
      0, 0, 0, 0, // OggS granule position
      0xFF, 0xFF, 0xFF, 0xFF, // OggS serial number
      0xFF, 0xFF, 0xFF, 0xFF, // OggS page sequence number
      0x01, // OggS page checksum
      0x00, // OggS page segment count
      0x00  // OggS packet table
  };

  outputFile.write(reinterpret_cast<const char*>(header), header_size);
  outputFile.write(reinterpret_cast<const char*>(data), size);
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
  OpusEncoder* encoder;
  encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, NULL);

  const float* pSampleData = (const float*)pInput;
  constexpr int max_data_bytes = 4000; // Adjust this based on your needs
  unsigned char output_data[max_data_bytes];
  int opus_bytes = opus_encode_float(encoder, pSampleData, frameCount, output_data, max_data_bytes);

  write_ogg_page(output_data, opus_bytes);

  opus_encoder_destroy(encoder);
}

int main() {
  outputFile.open("output.opus", std::ios::binary);
  if (!outputFile.is_open()) {
    std::cerr << "Failed to open output file\n";
    return -1;
  }

  write_ogg_header();
  write_opus_header();

  ma_context context;
  ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
  deviceConfig.capture.format = ma_format_f32;
  deviceConfig.capture.channels = CHANNELS;
  deviceConfig.sampleRate = SAMPLE_RATE;
  deviceConfig.dataCallback = data_callback;

  ma_device device;
  if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
    std::cerr << "Failed to initialize audio device\n";
    return -1;
  }

  if (ma_device_start(&device) != MA_SUCCESS) {
    std::cerr << "Failed to start audio device\n";
    ma_device_uninit(&device);
    outputFile.close();
    return -1;
  }

  std::cout << "Recording... Press Enter to stop.\n";
  std::cin.get();

  ma_device_uninit(&device);
  outputFile.close();

  return 0;
}
