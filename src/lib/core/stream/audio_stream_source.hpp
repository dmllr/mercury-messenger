#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

#include "core/stream/audio_capture.hpp"
#include "core/rtc/stream.hpp"
#include "logging/logging.h"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

#undef max

namespace hela::eillance {

class AudioStreamSource : public Stream::Source {
private:
  AudioCapture capture_;

  rtc::binary encoded_data_{};
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  std::uint64_t sample_time_{std::numeric_limits<std::uint64_t>::max()};

public:
  AudioStreamSource() {
    capture_.SetDataCallback([this](Packet packet) {
        encoded_data_.insert(
          encoded_data_.end(),
          packet.data.begin(),
          packet.data.end());
        sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(packet.timestamp - start_time_).count();
    });
  }

  void Start() final {
    capture_.Engage();
    start_time_ = std::chrono::steady_clock::now();
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  void LoadNextSample() final {
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = encoded_data_;
    encoded_data_.clear();

    LOGV << "Sending audio sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() final {
    capture_.Revoke();
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_time_;
  }

};

}  // namespace hela::eillance
