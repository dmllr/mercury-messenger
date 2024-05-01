#pragma once

#include <cstdint>

#include "rtc/common.hpp"


namespace hela::eillance {

struct Packet {
  rtc::binary data;
  std::uint32_t amount;

  void Clear() {
    data.clear();
    amount = 0;
  }
};

}  // namespace hela::eillance
