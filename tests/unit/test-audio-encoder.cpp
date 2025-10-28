#include "eillance/encoding/audio_encoder.hpp"

#include <gtest/gtest.h>

#include "eillance/capture/audio_config.hpp"

using AudioConfig = eillance::AudioConfig;
using AudioEncoderCli = eillance::AudioEncoderCli;

TEST(Audio, AudioEncoding) {
  static_assert(std::is_same_v<AudioConfig::SampleDataType, std::int16_t>, "This test isn't suppose to work on non-int16 data");

  auto frame{std::array<AudioConfig::SampleDataType, AudioConfig::frame_size>{}};
  for (size_t i = 0; i < AudioConfig::frame_size; ++i) {
    frame.at(i) = static_cast<AudioConfig::SampleDataType>(i);
  }
  auto encoder = AudioEncoderCli{};
  encoder.Engage();
  auto encoded_packet = encoder.EncodeBuffer(frame.data());
  ASSERT_GT(encoded_packet.size(), 3);
}
