#pragma once

namespace hela::eillance {

struct VideoConfig {
  static constexpr auto width = 1280;
  static constexpr auto height = 720;
  static constexpr auto frameRate = 30;
  static constexpr auto bitrate = static_cast<int>(0.22f * width * height * frameRate);
  static constexpr auto frameDuration = 1000000 / frameRate;
};

}  // namespace hela::eillance
