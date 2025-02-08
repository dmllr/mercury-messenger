#pragma once

#include <shared_mutex>
#include <utility>

#include <rtc/rtc.hpp>

namespace hela::eillance {

struct Peer {
  struct Track {
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::RtcpSrReporter> reporter;

    explicit Track(std::shared_ptr<rtc::Track> track)
        : track(std::move(track)),
          reporter(nullptr) {
    }

    Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> reporter)
        : track(std::move(track)),
          reporter(std::move(reporter)) {
    }
  };

  enum class State {
    Waiting,
    WaitingForVideo,
    WaitingForAudio,
    Ready
  };

  explicit Peer(std::shared_ptr<rtc::PeerConnection> pc) : peer_connection_(std::move(pc)) {}

  std::shared_ptr<Track> video_;
  std::shared_ptr<Track> audio_;
  std::shared_ptr<rtc::DataChannel> data_channel_;

  std::shared_ptr<rtc::PeerConnection> PeerConnection() {
    return peer_connection_;
  }

  void SetState(State state) {
    std::unique_lock lock(mutex_);
    this->state_ = state;
  }

  auto GetState() -> State {
    std::shared_lock lock(mutex_);
    return state_;
  }

private:
  std::shared_mutex mutex_;
  State state_ = State::Waiting;
  std::string id_;
  std::shared_ptr<rtc::PeerConnection> peer_connection_;
};

}  // namespace hela::eillance
