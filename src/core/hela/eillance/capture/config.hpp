#pragma once

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/capture/video_config.hpp"

namespace hela::eillance {

struct Config {
  AudioConfig audio_config;
  VideoConfig video_config;
};

}  // namespace hela::eillance
