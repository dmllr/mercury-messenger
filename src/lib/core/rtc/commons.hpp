#pragma once

#include <cstdint>
#include "rtc/common.hpp"


namespace hela::eillance {

struct Packet {
  rtc::binary data;
  std::uint64_t timestamp;
};

using DataCallback = std::function<void(Packet)>;

}  // namespace hela::eillance
