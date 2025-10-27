#pragma once

namespace eillance {

struct VideoConfigDefaults {
  static constexpr auto width = 1280U;
  static constexpr auto height = 720U;
  static constexpr auto rotation = 0U;
  static constexpr auto frame_rate = 30U;
  [[maybe_unused]] static constexpr auto lowest_bitrate_multiplier = 0.05;
  [[maybe_unused]] static constexpr auto low_bitrate_multiplier = 0.1;
  [[maybe_unused]] static constexpr auto reduced_bitrate_multiplier = 0.15;
  [[maybe_unused]] static constexpr auto common_bitrate_multiplier = 0.22;
  [[maybe_unused]] static constexpr auto good_bitrate_multiplier = 0.3;
  [[maybe_unused]] static constexpr auto high_bitrate_multiplier = 0.4;
  [[maybe_unused]] static constexpr auto highest_bitrate_multiplier = 0.5;
  static constexpr auto bitrate = static_cast<int>(common_bitrate_multiplier * width * height * frame_rate);
};

struct VideoConfig {
  int camera_id{};
  std::uint32_t width{VideoConfigDefaults::width};
  std::uint32_t height{VideoConfigDefaults::height};
  std::uint32_t rotation{VideoConfigDefaults::rotation};
  std::uint16_t frame_rate{VideoConfigDefaults::frame_rate};
  std::uint32_t bitrate{VideoConfigDefaults::bitrate};

  auto operator==(const VideoConfig& other) const noexcept -> bool {
    return camera_id == other.camera_id && width == other.width && height == other.height && frame_rate == other.frame_rate && bitrate == other.bitrate;
  }
};

}  // namespace eillance
