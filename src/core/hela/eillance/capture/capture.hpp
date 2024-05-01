#pragma once

#include "hela/eillance/common/packet.hpp"
#include <memory>

namespace hela::eillance {

class Capture {
public:
  template <typename T>
  explicit Capture(std::unique_ptr<T> x) : self_(new CaptureModel<T>(std::move(x))) {}

  Capture(const Capture &x) = delete;

  Capture(Capture &&) noexcept = default;

  Capture &operator=(const Capture &x) = delete;

  Capture &operator=(Capture &&) noexcept = default;

  void Engage() {
    self_->Engage();
  }
  void Revoke() {
    self_->Revoke();
  }
  Packet FetchBuffers() {
    return self_->FetchBuffers();
  }

private:
  struct CaptureInterface {
    virtual ~CaptureInterface() = default;
    virtual void Engage() = 0;
    virtual void Revoke() = 0;
    virtual Packet FetchBuffers() = 0;
  };

public:
  template <typename T>
  class CaptureModel final : public CaptureInterface {
    std::unique_ptr<T> capture_;
  public:
    explicit CaptureModel(std::unique_ptr<T> x) : capture_(std::move(x)) {}
    void Engage() final {
      capture_->Engage();
    }
    void Revoke() final {
      capture_->Revoke();
    }
    Packet FetchBuffers() final {
      return capture_->FetchBuffers();
    }
  };

private:
  std::unique_ptr<CaptureInterface> self_;
};

}  // namespace hela::eillance