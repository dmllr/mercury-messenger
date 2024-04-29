#pragma once

#include "rtc/common.hpp"
#include <cstdint>


namespace hela::eillance {

struct Packet {
  rtc::binary data;
  std::uint64_t timestamp;
};

using DataCallback = std::function<void(Packet)>;

}  // namespace hela::eillance
