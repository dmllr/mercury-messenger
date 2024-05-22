#pragma once

#include <memory>

#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/utils/invoke_configure_video.hpp"

namespace hela::eillance {

class Capture {
public:
  template<typename T>
  explicit Capture(std::unique_ptr<T> impl) : self_(new CaptureModel<T>(std::move(impl))) {}
  Capture(const Capture&) = delete;
  Capture(Capture&&) noexcept = default;
  Capture& operator=(const Capture&) = delete;
  Capture& operator=(Capture&&) noexcept = default;
  ~Capture() = default;

  void Engage() {
    self_->Engage();
  }
  void Revoke() {
    self_->Revoke();
  }
  Packet FetchBuffers() {
    return self_->FetchBuffers();
  }
  Time::Unit GetSampleDuration() {
    return self_->GetSampleDuration();
  }
  void ConfigureVideo(VideoConfig conf) {
    return self_->ConfigureVideo(std::move(conf));
  }


private:
  struct CaptureInterface {
    CaptureInterface() = default;
    CaptureInterface(const CaptureInterface&) = delete;
    CaptureInterface(CaptureInterface&&) noexcept = delete;
    CaptureInterface& operator=(const CaptureInterface&) = delete;
    CaptureInterface& operator=(CaptureInterface&&) noexcept = delete;
    virtual ~CaptureInterface() = default;
    virtual void Engage() = 0;
    virtual void Revoke() = 0;
    virtual void ConfigureVideo(VideoConfig conf) = 0;
    virtual auto FetchBuffers() -> Packet = 0;
    virtual auto GetSampleDuration() -> Time::Unit = 0;
  };

public:
  template<typename T>
  class CaptureModel final : public CaptureInterface {
    std::unique_ptr<T> capture_;

  public:
    explicit CaptureModel(std::unique_ptr<T> impl) : capture_(std::move(impl)) {}
    void Engage() final {
      capture_->Engage();
    }
    void Revoke() final {
      capture_->Revoke();
    }
    void ConfigureVideo(VideoConfig conf) final {
      InvokeConfigureVideo(capture_, conf);
    }
    auto FetchBuffers() -> Packet final {
      return capture_->FetchBuffers();
    }
    auto GetSampleDuration() -> Time::Unit final {
      return capture_->GetSampleDuration();
    }
  };

private:
  std::unique_ptr<CaptureInterface> self_;
};

}  // namespace hela::eillance