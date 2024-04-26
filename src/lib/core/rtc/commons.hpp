#pragma once

#include <cstdint>
#include "rtc/common.hpp"


namespace hela::eillance {

struct Packet {
  rtc::binary data;
  std::chrono::time_point<std::chrono::steady_clock> timestamp;
};

using DataCallback = std::function<void(Packet)>;

}  // namespace hela::eillance
