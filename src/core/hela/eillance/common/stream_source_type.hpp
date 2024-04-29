#pragma once

#include "rtc/common.hpp"
#include <cstdint>


namespace hela::eillance {

enum class StreamSourceType : std::uint8_t {
  audio,
  video
};

}  // namespace hela::eillance
