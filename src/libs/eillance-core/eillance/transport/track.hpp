#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace eillance::transport {

class ITrack {
public:
  virtual ~ITrack() = default;
  virtual void SendSample(const std::vector<std::byte>& sample, uint64_t sample_time) = 0;
};

} // namespace eillance::transport
