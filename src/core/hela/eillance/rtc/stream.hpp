#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <utility>

#include <rtc/rtc.hpp>

#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/rtc/peer.hpp"


namespace hela::eillance {

class Stream : std::enable_shared_from_this<Stream> {
public:
  class Source {
  public:
    Source() = default;
    Source(Source&&) = delete;
    Source(const Source&) = delete;
    Source& operator=(Source&&) = delete;
    Source& operator=(const Source&) = delete;

    enum class Type : std::uint8_t {
      Audio,
      Video
    };

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void LoadNextSample() = 0;

    virtual auto GetSampleTime() -> uint64_t = 0;
    virtual auto GetSampleDuration() -> uint64_t = 0;
    virtual auto GetSample() -> rtc::binary = 0;

  protected:
    ~Source() = default;
  };

private:
  DispatchQueue dispatchQueue = DispatchQueue("StreamQueue", 1);

  bool isRunning = false;

  const std::shared_ptr<Source> audio_;
  const std::shared_ptr<Source> video_;

public:
  Stream(std::shared_ptr<Source> video, std::shared_ptr<Source> audio) : audio_(std::move(audio)), video_(std::move(video)) {}
  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  auto operator=(const Stream&) -> Stream& = delete;
  auto operator=(Stream&&) -> Stream& = delete;

  ~Stream() {
    Stop();
  }

private:
  rtc::synchronized_callback<Source::Type, uint64_t, rtc::binary> sampleHandler_;

  auto UnsafePrepareForSample() -> std::pair<std::shared_ptr<Source>, Source::Type> {
    std::shared_ptr<Source> ss;
    Source::Type sst;
    uint64_t next_time;
    if (audio_->GetSampleTime() < video_->GetSampleTime()) {
      ss = audio_;
      sst = Source::Type::Audio;
      next_time = audio_->GetSampleTime();
    } else {
      ss = video_;
      sst = Source::Type::Video;
      next_time = video_->GetSampleTime();
    }

    auto current_time = Time::Now();

    TELEMETRY(
      LOGD << "AT: " << audio_->GetSampleTime() << "\n"
           << "VT: " << video_->GetSampleTime() << "\n"
           << " T: " << ((sst == Source::Type::Audio) ? "Audio" : "Video") << "\n";
    )
    if (next_time > current_time) {
      auto wait_time = next_time - current_time;
      std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
    }
    return {ss, sst};
  }

  void SendSample() {
    if (!isRunning) {
      return;
    }
    auto [ss, sst] = UnsafePrepareForSample();
    auto sample = ss->GetSample();
    sampleHandler_(sst, ss->GetSampleTime(), sample);
    ss->LoadNextSample();
    dispatchQueue.dispatch([this]() {
      this->SendSample();
    });
  }

public:
  void OnSample(std::function<void(Source::Type, uint64_t, rtc::binary)> handler) {
    sampleHandler_ = std::move(handler);
  }

  void Start() {
    if (isRunning) {
      return;
    }
    isRunning = true;
    video_->Start();
    audio_->Start();
    dispatchQueue.dispatch([this]() {
      this->SendSample();
    });
  }

  void Stop() {
    if (!isRunning) {
      return;
    }
    isRunning = false;
    dispatchQueue.removePending();
    audio_->Stop();
    video_->Stop();
  }
};

}  // namespace hela::eillance
