#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "core/rtc/dispatch_queue.hpp"
#include "core/rtc/peer.hpp"
#include "rtc/rtc.hpp"

class Stream : std::enable_shared_from_this<Stream> {
public:
  class Source {
  public:
    enum class Type : std::uint8_t {
      Audio,
      Video
    };

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void LoadNextSample() = 0;

    virtual auto GetSampleTime_us() -> uint64_t = 0;
    virtual auto GetSample() -> rtc::binary = 0;
  };

private:
  uint64_t startTime = 0;
  std::mutex mutex;
  DispatchQueue dispatchQueue = DispatchQueue("StreamQueue");

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
    uint64_t nextTime;
    if (audio_->GetSampleTime_us() < video_->GetSampleTime_us()) {
      ss = audio_;
      sst = Source::Type::Audio;
      nextTime = audio_->GetSampleTime_us();
    } else {
      ss = video_;
      sst = Source::Type::Video;
      nextTime = video_->GetSampleTime_us();
    }

    auto current_time = currentTimeInMicroSeconds();

    auto elapsed = current_time - startTime;
    if (nextTime > elapsed) {
      auto wait_time = nextTime - elapsed;
      mutex.unlock();
      if (wait_time > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
      }
      mutex.lock();
    }
    return {ss, sst};
  }

  void SendSample() {
    const auto lock{std::unique_lock(mutex)};
    if (!isRunning) {
      return;
    }
    auto ssSST = UnsafePrepareForSample();
    auto ss = ssSST.first;
    auto sst = ssSST.second;
    auto sample = ss->GetSample();
    sampleHandler_(sst, ss->GetSampleTime_us(), sample);
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
    const auto lock{std::unique_lock(mutex)};
    if (isRunning) {
      return;
    }
    isRunning = true;
    startTime = currentTimeInMicroSeconds();
    audio_->Start();
    video_->Start();
    dispatchQueue.dispatch([this]() {
      this->SendSample();
    });
  }

  void Stop() {
    const auto lock{std::unique_lock(mutex)};
    if (!isRunning) {
      return;
    }
    isRunning = false;
    dispatchQueue.removePending();
    audio_->Stop();
    video_->Stop();
  }
};
