#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "eillance/capture/config.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance {

class Stream : std::enable_shared_from_this<Stream> {
public:
  // Using alias to avoid redundant namespace qualification
  using Peer = eillance::transport::Peer;

  class Source {
  public:
    Source() = default;
    Source(Source&&) = delete;
    Source(const Source&) = delete;
    Source& operator=(Source&&) = delete;
    Source& operator=(const Source&) = delete;

    enum class Type : std::uint8_t {
      audio,
      video
    };

    virtual void Configure(Config config) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void LoadNextSample() = 0;

    virtual auto GetSampleTime() -> std::uint64_t = 0;
    virtual auto GetSampleDuration() -> std::uint64_t = 0;
    virtual auto GetSample() -> std::vector<std::byte> = 0;

  protected:
    ~Source() = default;
  };

private:
  DispatchQueue dispatchQueue_ = DispatchQueue("StreamQueue", 1);
  std::shared_ptr<Source> audio_;
  std::shared_ptr<Source> video_;
  bool isRunning_ = false;

public:
  Stream(std::shared_ptr<Source> video, std::shared_ptr<Source> audio) : audio_(std::move(audio)), video_(std::move(video)) {}
  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  auto operator=(const Stream&) -> Stream& = delete;
  auto operator=(Stream&&) -> Stream& = delete;

  auto Audio() {
    return audio_;
  }

  auto Video() {
    return video_;
  }

  ~Stream() {
    Stop();
  }

private:
  std::function<void(Source::Type, std::uint64_t, const std::vector<std::byte>&)> sampleHandlerFunc_;
  std::mutex sampleHandlerMutex_;

  auto UnsafePrepareForSample() const -> std::pair<std::shared_ptr<Source>, Source::Type> {
    std::shared_ptr<Source> src;
    Source::Type srct{};
    std::uint64_t next_time{};
    if (audio_->GetSampleTime() < video_->GetSampleTime()) {
      src = audio_;
      srct = Source::Type::audio;
      next_time = audio_->GetSampleTime();
    } else {
      src = video_;
      srct = Source::Type::video;
      next_time = video_->GetSampleTime();
    }

    const auto current_time = Time::Now();

    if (next_time > current_time) {
      const auto wait_time = next_time - current_time;
      std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
    }
    return {src, srct};
  }

  void SendSample() {
    if (!isRunning_) {
      return;
    }
    auto [ss, sst] = UnsafePrepareForSample();
    const auto sample = ss->GetSample();

    std::function<void(Source::Type, std::uint64_t, const std::vector<std::byte>&)> handler;
    {
      std::lock_guard<std::mutex> lock(sampleHandlerMutex_);
      handler = sampleHandlerFunc_;
    }
    if (handler) {
      handler(sst, ss->GetSampleTime(), sample);
    }

    ss->LoadNextSample();
    dispatchQueue_.Dispatch([this]() {
      this->SendSample();
    });
  }

public:
  void OnSample(std::function<void(Source::Type, std::uint64_t, const std::vector<std::byte>&)> handler) {
    std::lock_guard<std::mutex> lock(sampleHandlerMutex_);
    sampleHandlerFunc_ = std::move(handler);
  }

  auto IsRunning() const {
    return isRunning_;
  }

  void Start() {
    if (isRunning_) {
      return;
    }
    isRunning_ = true;
    video_->Start();
    audio_->Start();
    dispatchQueue_.Dispatch([this]() {
      this->SendSample();
    });
  }

  void Stop() {
    if (!isRunning_) {
      return;
    }
    isRunning_ = false;
    dispatchQueue_.RemovePending();
    audio_->Stop();
    video_->Stop();
  }
};

}  // namespace eillance
