#pragma once

#include "eillance/capture/audio_config.hpp"
#include "eillance/capture/video_config.hpp"

namespace eillance {

struct Config {
  AudioConfig audio_config;
  VideoConfig video_config;
};

}  // namespace eillance
