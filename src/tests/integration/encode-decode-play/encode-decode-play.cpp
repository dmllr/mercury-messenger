#include <miniaudio.h>
#include <opus.h>
#include <iostream>
#include <vector>

#include "hela/eillance/capture/audio_config.hpp"

using AudioConfig = hela::eillance::AudioConfig;

std::vector<std::vector<unsigned char>> opus_data;
int frame = 0;

// Callback function for handling captured audio data
void on_send_frames_to_opus(ma_device* pDevice, void*, const void* pInput, ma_uint32 frameCount) {
  auto* encoder = static_cast<OpusEncoder*>(pDevice->pUserData);

  std::cout << "FC " << frameCount << ". ";

  // Encode the input PCM data to Opus
  unsigned char opus_frame[4096];
  int opus_length = AudioConfig::opusEncodeFunction(encoder, static_cast<const opus_int16*>(pInput), AudioConfig ::frameSize, opus_frame, sizeof(opus_frame));
  if (opus_length < 0) {
    std::cerr << "Error encoding to Opus" << std::endl;
    return;
  } else if (opus_length == 1) {
    std::cerr << "Opus encoded to 1 byte" << std::endl;
    return;
  }

  std::cout << "Opus  encoded to " << opus_length << std::endl;

  // Store the encoded Opus data
  opus_data.emplace_back(opus_frame, opus_frame + opus_length);
}

// Callback function for handling decoded audio data
void on_receive_frames_from_opus(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount) {
  auto* decoder = static_cast<OpusDecoder*>(pDevice->pUserData);

  if (frame >= opus_data.size()) {
    return;
  }

  // Decode the input Opus data to PCM
  const auto pcm_length = opus_decode(decoder, reinterpret_cast<const unsigned char*>(opus_data[frame].data()), frameCount, static_cast<opus_int16*>(pOutput), AudioConfig ::frameSize, 0);
  if (pcm_length < 0) {
    std::cerr << "Error decoding from Opus" << std::endl;
    return;
  }
  frame++;
}

int main() {
  // Initialize Opus encoder and decoder
  int error{0};
  OpusEncoder* encoder = opus_encoder_create(AudioConfig ::sampleRate, AudioConfig ::channels, OPUS_APPLICATION_AUDIO, &error);
  if (error != OPUS_OK) {
    std::cerr << "Error initializing Opus encoder: " << opus_strerror(error) << std::endl;
    return -1;
  }

  OpusDecoder* decoder = opus_decoder_create(AudioConfig ::sampleRate, AudioConfig ::channels, &error);
  if (error != OPUS_OK) {
    std::cerr << "Error initializing Opus decoder: " << opus_strerror(error) << std::endl;
    opus_encoder_destroy(encoder);
    return -1;
  }

  // Initialize miniaudio for capturing
  ma_context context;
  ma_device_config capture_device_config;
  ma_device capture_device;

  auto result = ma_context_init(nullptr, 0, nullptr, &context);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize miniaudio context" << std::endl;
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    return -1;
  }

  capture_device_config = ma_device_config_init(ma_device_type_capture);
  capture_device_config.capture.format = AudioConfig::maSampleFormat;
  capture_device_config.capture.channels = AudioConfig ::channels;
  capture_device_config.sampleRate = AudioConfig ::sampleRate;
  capture_device_config.dataCallback = on_send_frames_to_opus;
  capture_device_config.pUserData = encoder;

  result = ma_device_init(nullptr, &capture_device_config, &capture_device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize miniaudio capture device" << std::endl;
    ma_context_uninit(&context);
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    return -1;
  }

  // Initialize miniaudio for playback
  ma_device_config playback_device_config;
  ma_device playback_device;

  playback_device_config = ma_device_config_init(ma_device_type_playback);
  playback_device_config.playback.format = AudioConfig::maSampleFormat;
  playback_device_config.playback.channels = AudioConfig ::channels;
  playback_device_config.sampleRate = AudioConfig ::sampleRate;
  playback_device_config.dataCallback = on_receive_frames_from_opus;
  playback_device_config.pUserData = decoder;

  result = ma_device_init(nullptr, &playback_device_config, &playback_device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to initialize miniaudio playback device" << std::endl;
    ma_device_uninit(&capture_device);
    ma_context_uninit(&context);
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    return -1;
  }

  // Start the capture device
  result = ma_device_start(&capture_device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to start miniaudio capture device" << std::endl;
    ma_device_uninit(&playback_device);
    ma_device_uninit(&capture_device);
    ma_context_uninit(&context);
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    return -1;
  }

  // Sleep for 2 seconds (or you can use a more sophisticated method to wait for input)
  ma_sleep(2000);

  // Stop the capture device
  ma_device_stop(&capture_device);
  ma_device_uninit(&capture_device);

  // Start the playback device
  result = ma_device_start(&playback_device);
  if (result != MA_SUCCESS) {
    std::cerr << "Failed to start miniaudio playback device" << std::endl;
    ma_device_uninit(&playback_device);
    ma_context_uninit(&context);
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    return -1;
  }

  // Sleep for 2 seconds to allow playback to finish
  ma_sleep(2000);

  // Stop the playback device
  ma_device_stop(&playback_device);
  ma_device_uninit(&playback_device);

  // Uninitialize miniaudio
  ma_context_uninit(&context);

  // Clean up Opus encoder and decoder
  opus_encoder_destroy(encoder);
  opus_decoder_destroy(decoder);

  return 0;
}
