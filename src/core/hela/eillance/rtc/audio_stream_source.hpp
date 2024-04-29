#pragma once

#include <cstdint>

#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/stream.hpp"

namespace hela::eillance {

class AudioStreamSource : public Stream::Source {
private:
  static constexpr auto id = CaptureManager::ConsumerID::rtcAudio;
  std::shared_ptr<CaptureManager> capture_manager_;
  rtc::binary encoded_data_{};
  std::uint64_t start_time_{};
  std::uint64_t sample_time_{};

public:
  AudioStreamSource(std::shared_ptr<CaptureManager> capture_manager)
      : capture_manager_(capture_manager) {}

  void Start() final {
    capture_manager_->RegisterConsumer(id, StreamSourceType::audio);
    start_time_ = Time::Now();
    sample_time_ = start_time_;
  }

  auto GetSample() -> rtc::binary final {
    return capture_manager_->GetSample(id);
  }

  void Stop() final {
    capture_manager_->DeregisterConsumer(id);
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_time_ - start_time_;
  }
};

}  // namespace hela::eillance
