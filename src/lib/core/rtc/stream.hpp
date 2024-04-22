#pragma once

#include <chrono>
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
    enum class Type {
      Audio,
      Video
    };

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void loadNextSample() = 0;

    virtual auto getSampleTime_us() -> uint64_t = 0;
    virtual auto getSampleDuration_us() -> uint64_t = 0;
    virtual auto getSample() -> rtc::binary = 0;
  };

private:
  uint64_t startTime = 0;
  std::mutex mutex;
  DispatchQueue dispatchQueue = DispatchQueue("StreamQueue");

  bool isRunning = false;

  const std::shared_ptr<Source> audio;
  const std::shared_ptr<Source> video;

public:
  Stream(std::shared_ptr<Source> video, std::shared_ptr<Source> audio) : video(std::move(video)), audio(std::move(audio)) {}
  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  auto operator=(const Stream&) -> Stream& = delete;
  auto operator=(Stream&&) -> Stream& = delete;

  ~Stream() {
    stop();
  }

private:
  rtc::synchronized_callback<Source::Type, uint64_t, rtc::binary> sampleHandler;

  auto unsafePrepareForSample() -> std::pair<std::shared_ptr<Source>, Source::Type> {
    std::shared_ptr<Source> ss;
    Source::Type sst;
    uint64_t nextTime;
    if (audio->getSampleTime_us() < video->getSampleTime_us()) {
      ss = audio;
      sst = Source::Type::Audio;
      nextTime = audio->getSampleTime_us();
    } else {
      ss = video;
      sst = Source::Type::Video;
      nextTime = video->getSampleTime_us();
    }

    auto currentTime = currentTimeInMicroSeconds();

    auto elapsed = currentTime - startTime;
    if (nextTime > elapsed) {
      auto waitTime = nextTime - elapsed;
      mutex.unlock();
      std::this_thread::sleep_for(std::chrono::microseconds(waitTime));
      mutex.lock();
    }
    return {ss, sst};
  }

  void sendSample() {
    std::lock_guard lock(mutex);
    if (!isRunning) {
      return;
    }
    auto ssSST = unsafePrepareForSample();
    auto ss = ssSST.first;
    auto sst = ssSST.second;
    auto sample = ss->getSample();
    sampleHandler(sst, ss->getSampleTime_us(), sample);
    ss->loadNextSample();
    dispatchQueue.dispatch([this]() {
      this->sendSample();
    });
  }

public:
  void onSample(std::function<void(Source::Type, uint64_t, rtc::binary)> handler) {
    sampleHandler = std::move(handler);
  }

  void start() {
    std::lock_guard lock(mutex);
    if (isRunning) {
      return;
    }
    isRunning = true;
    startTime = currentTimeInMicroSeconds();
    audio->start();
    video->start();
    dispatchQueue.dispatch([this]() {
      this->sendSample();
    });
  }
  void stop() {
    std::lock_guard lock(mutex);
    if (!isRunning) {
      return;
    }
    isRunning = false;
    dispatchQueue.removePending();
    audio->stop();
    video->stop();
  }
};
