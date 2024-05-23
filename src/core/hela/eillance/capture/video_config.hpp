#pragma once

#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

struct VideoConfigDefaults {
  static constexpr auto width = 1280;
  static constexpr auto height = 720;
  static constexpr auto frameRate = 30;
  [[maybe_unused]] static constexpr auto lowestBitrateMultiplier = 0.05;
  [[maybe_unused]] static constexpr auto lowBitrateMultiplier = 0.1;
  [[maybe_unused]] static constexpr auto reducedBitrateMultiplier = 0.15;
  [[maybe_unused]] static constexpr auto commonBitrateMultiplier = 0.22;
  [[maybe_unused]] static constexpr auto goodBitrateMultiplier = 0.3;
  [[maybe_unused]] static constexpr auto highBitrateMultiplier = 0.4;
  [[maybe_unused]] static constexpr auto highestBitrateMultiplier = 0.5;
  static constexpr auto bitrate = static_cast<int>(commonBitrateMultiplier * width * height * frameRate);
  static constexpr auto frameDuration = Time::uS / frameRate;
};

struct VideoConfig {
  int cameraId{};
  std::uint32_t width{VideoConfigDefaults::width};
  std::uint32_t height{VideoConfigDefaults::height};
  std::uint8_t frameRate{VideoConfigDefaults::frameRate};
  std::uint32_t bitrate{VideoConfigDefaults::bitrate};
};

}  // namespace hela::eillance
