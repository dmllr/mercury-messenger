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

class Stream : public std::enable_shared_from_this<Stream> {
 public:
  // Using alias to avoid redundant namespace qualification
  using Peer = eillance::transport::Peer;

  class Source {
   public:
    Source() = default;
    Source(Source&&) = delete;
    Source(Source const&) = delete;
    auto operator=(Source&&) -> Source& = delete;
    auto operator=(Source const&) -> Source& = delete;

    enum class Type : std::uint8_t { audio, video };

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
  bool isRunning_{false};

 public:
  Stream(std::shared_ptr<Source> video, std::shared_ptr<Source> audio)
      : audio_(std::move(audio)),
        video_(std::move(video)) {}
  Stream(Stream const&) = delete;
  Stream(Stream&&) = delete;
  auto operator=(Stream const&) -> Stream& = delete;
  auto operator=(Stream&&) -> Stream& = delete;

  auto Audio() { return audio_; }

  auto Video() { return video_; }

  ~Stream() { Stop(); }

 private:
  std::function<void(Source::Type, std::uint64_t, std::vector<std::byte> const&)> sampleHandlerFunc_;
  std::mutex sampleHandlerMutex_;

  [[nodiscard]] auto UnsafePrepareForSample() -> std::pair<std::shared_ptr<Source>, Source::Type> {
    std::shared_ptr<Source> src;
    Source::Type src_type{};
    std::uint64_t next_time{};
    if (audio_->GetSampleTime() < video_->GetSampleTime()) {
      src = audio_;
      src_type = Source::Type::audio;
      next_time = audio_->GetSampleTime();
    } else {
      src = video_;
      src_type = Source::Type::video;
      next_time = video_->GetSampleTime();
    }

    auto const current_time = Time::Now();

    if (next_time > current_time) {
      auto const wait_time = next_time - current_time;
      std::this_thread::sleep_for(std::chrono::microseconds(wait_time));
    }
    return {src, src_type};
  }

  void SendSample() {
    if (!isRunning_) {
      return;
    }
    auto [source, source_type] = UnsafePrepareForSample();
    auto const sample = source->GetSample();

    std::function<void(Source::Type, std::uint64_t, std::vector<std::byte> const&)> handler;
    {
      std::lock_guard<std::mutex> const lock(sampleHandlerMutex_);
      handler = sampleHandlerFunc_;
    }
    if (handler) {
      handler(source_type, source->GetSampleTime(), sample);
    }

    source->LoadNextSample();
    dispatchQueue_.Dispatch([this]() { this->SendSample(); });
  }

 public:
  void OnSample(std::function<void(Source::Type, std::uint64_t, std::vector<std::byte> const&)> handler) {
    std::lock_guard<std::mutex> const lock(sampleHandlerMutex_);
    sampleHandlerFunc_ = std::move(handler);
  }

  [[nodiscard]] auto IsRunning() const -> bool { return isRunning_; }

  void Start() {
    if (isRunning_) {
      return;
    }
    isRunning_ = true;
    video_->Start();
    audio_->Start();
    dispatchQueue_.Dispatch([this]() { this->SendSample(); });
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
