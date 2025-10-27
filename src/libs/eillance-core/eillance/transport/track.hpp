#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace eillance::transport {

class ITrack {
public:
  virtual ~ITrack() = default;
  virtual void SendSample(const std::vector<std::byte>& sample, uint64_t sample_time) = 0;
};

}  // namespace eillance::transport
