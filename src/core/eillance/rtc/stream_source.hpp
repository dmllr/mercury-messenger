#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/rtc/stream.hpp"

namespace eillance {

class StreamSource final : public Stream::Source {
private:
  Capture capture_;

  Packet sample_{};
  rtc::binary encoded_data_{};

public:
  ~StreamSource() = default;
  explicit StreamSource(Capture capture) : capture_(std::move(capture)){};
  StreamSource(StreamSource&&) = delete;
  StreamSource(const StreamSource&) = delete;
  StreamSource& operator=(StreamSource&&) = delete;
  StreamSource& operator=(const StreamSource&) = delete;

  void Configure(Config config) final {
    capture_.Configure(config);
  }

  void Start() final {
    capture_.Engage();
  }

  void LoadNextSample() final {
    sample_ = capture_.FetchBuffers();
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

  auto GetSampleDuration() -> uint64_t final {
    return capture_.GetSampleDuration();
  }
};

}  // namespace eillance
