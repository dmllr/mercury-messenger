#pragma once

namespace hela::eillance {

struct VideoConfigDefaults {
  static constexpr auto width = 1280;
  static constexpr auto height = 720;
  static constexpr auto frameRate = 30;
  static constexpr auto bitrate = static_cast<int>(0.22f * width * height * frameRate);
};

struct VideoConfig {
  std::uint32_t width{VideoConfigDefaults::width};
  std::uint32_t height{VideoConfigDefaults::height};
  std::uint8_t frameRate{VideoConfigDefaults::frameRate};
  std::uint32_t bitrate{VideoConfigDefaults::bitrate};
};

}  // namespace hela::eillance
