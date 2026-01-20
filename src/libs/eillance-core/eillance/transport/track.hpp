#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eillance::transport {

class ITrack {
 public:
  virtual ~ITrack() = default;
  virtual void SendSample(std::vector<std::byte> const& sample, uint64_t sample_time) = 0;
};

}  // namespace eillance::transport
