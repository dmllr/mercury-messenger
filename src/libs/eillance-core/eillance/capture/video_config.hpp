#pragma once

#include <cstdint>

namespace eillance {

struct VideoConfigDefaults {
  static constexpr auto width = 1280U;
  static constexpr auto height = 720U;
  static constexpr auto rotation = 0U;
  static constexpr auto frameRate = 30U;
  // Multipliers for bitrate calculation
  [[maybe_unused]] static constexpr auto lowestBitrateMultiplier = 0.05;
  [[maybe_unused]] static constexpr auto lowBitrateMultiplier = 0.1;
  [[maybe_unused]] static constexpr auto reducedBitrateMultiplier = 0.15;
  [[maybe_unused]] static constexpr auto commonBitrateMultiplier = 0.22;
  [[maybe_unused]] static constexpr auto goodBitrateMultiplier = 0.3;
  [[maybe_unused]] static constexpr auto highBitrateMultiplier = 0.4;
  [[maybe_unused]] static constexpr auto highestBitrateMultiplier = 0.5;
  static constexpr auto bitrate = static_cast<int>(commonBitrateMultiplier * width * height * frameRate);
};

struct VideoConfig {
  int camera_id{};
  std::uint32_t width{VideoConfigDefaults::width};
  std::uint32_t height{VideoConfigDefaults::height};
  std::uint32_t rotation{VideoConfigDefaults::rotation};
  std::uint16_t frame_rate{VideoConfigDefaults::frameRate};
  std::uint32_t bitrate{VideoConfigDefaults::bitrate};

  auto operator==(VideoConfig const& other) const noexcept -> bool {
    return camera_id == other.camera_id && width == other.width && height == other.height &&
           frame_rate == other.frame_rate && bitrate == other.bitrate;
  }
};

}  // namespace eillance
