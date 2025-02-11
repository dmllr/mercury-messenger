#pragma once

#include <shared_mutex>
#include <utility>

#include <rtc/rtc.hpp>

namespace hela::eillance {

struct Peer {
  class Track {
    std::shared_ptr<rtc::Track> track_;
    std::shared_ptr<rtc::RtcpSrReporter> reporter_;

  public:
    explicit Track(std::shared_ptr<rtc::Track> track)
        : track_(std::move(track)),
          reporter_(nullptr) {
    }

    Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> reporter)
        : track_(std::move(track)),
          reporter_(std::move(reporter)) {
    }

    auto Send(const rtc::binary& message) {
      return track_->send(message);
    }

    auto Reporter() {
      return reporter_;
    }
  };

  enum class State {
    waiting,
    waitingForVideo,
    waitingForAudio,
    ready
  };

  explicit Peer(std::shared_ptr<rtc::PeerConnection> pc) : peer_connection_(std::move(pc)) {}

  std::shared_ptr<Track> video_;
  std::shared_ptr<Track> audio_;
  std::shared_ptr<rtc::DataChannel> data_channel_;
  std::shared_ptr<rtc::PeerConnection> peer_connection_;

  void SetState(State state) {
    const auto lock = std::unique_lock(mutex_);
    this->state_ = state;
  }

  auto GetState() -> State {
    const auto lock = std::unique_lock(mutex_);
    return state_;
  }

private:
  std::shared_mutex mutex_;
  State state_ = State::waiting;
  std::string id_;
};

}  // namespace hela::eillance
