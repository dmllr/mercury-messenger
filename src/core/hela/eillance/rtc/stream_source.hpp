#pragma once

#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "hela/eillance/capture/capture.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/time.hpp"

namespace hela::eillance {

class StreamSource : public Stream::Source {
private:
  Capture capture_;

  std::uint64_t start_time_{};
  Packet sample_{};
  rtc::binary encoded_data_{};

public:
  StreamSource(Capture capture) : capture_(std::move(capture)) {};

  void Start() final {
    capture_.Engage();
    start_time_ = Time::Now();
  }

  void LoadNextSample() final {
    sample_ = std::move(capture_.FetchBuffers());
  }

  auto GetSample() -> rtc::binary final {
    return sample_.buffer();
  }

  void Stop() final {
    capture_.Revoke();
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_.timestamp();
  }
};

}  // namespace hela::eillance
