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
  std::uint64_t sample_time_{start_time_};
  Packet sample_{};
  rtc::binary encoded_data_{};

public:
  StreamSource(Capture capture) : capture_(std::move(capture)) {};

  void Start() final {
    capture_.Engage();
    start_time_ = Time::Now();
    sample_time_ = start_time_;
  }

  void LoadNextSample() final {
    sample_ = capture_.FetchBuffers();
    sample_time_ = Time::Now();
  }

  auto GetSample() -> rtc::binary final {
    return sample_.data;
  }

  void Stop() final {
    capture_.Revoke();
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_time_ - start_time_;
  }
};

}  // namespace hela::eillance
