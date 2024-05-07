#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/capture/audio_encoder.hpp"

#include <gtest/gtest.h>

using AudioConfig = hela::eillance::AudioConfig;
using AudioEncoder = hela::eillance::AudioEncoder;

TEST(Audio, AudioEncoding) {
  static_assert(std::is_same_v<AudioConfig::SampleDataType, std::int16_t>, "This test isn't suppose to work on non-int16 data");

  auto  frame{std::array<AudioConfig::SampleDataType, AudioConfig::frameSize>{}};
  for (AudioConfig::SampleDataType i = 0; i < AudioConfig::frameSize; ++i) {
    frame.at(i) = i;
  }
  auto encoder = AudioEncoder{};
  encoder.Engage();
  auto encoded_packet = encoder.EncodeBuffer(frame.data());
  ASSERT_GT(encoded_packet.size(), 3);
}
