#pragma once

#include <shared_mutex>
#include <utility>

#include <rtc/rtc.hpp>

namespace hela::eillance {

struct Peer {
  struct Track {
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::RtcpSrReporter> sender;
    std::shared_ptr<rtc::RtcpReceivingSession> session;

    Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpSrReporter> sender) {
      this->track = std::move(track);
      this->sender = std::move(sender);
      this->session = nullptr;
    }

    Track(std::shared_ptr<rtc::Track> track, std::shared_ptr<rtc::RtcpReceivingSession> session) {
      this->track = std::move(track);
      this->sender = nullptr;
      this->session = std::move(session);
    }
  };

  enum class State {
    Waiting,
    WaitingForVideo,
    WaitingForAudio,
    Ready
  };

  explicit Peer(std::shared_ptr<rtc::PeerConnection> pc) : peer_connection_(std::move(pc)) {}

  std::optional<std::shared_ptr<Track>> video;
  std::optional<std::shared_ptr<Track>> audio;
  std::optional<std::shared_ptr<rtc::DataChannel>> dataChannel;

  std::shared_ptr<rtc::PeerConnection> PeerConnection() {
    return peer_connection_;
  }

  void setState(State state) {
    std::unique_lock lock(_mutex);
    this->state = state;
  }

  auto getState() -> State {
    std::shared_lock lock(_mutex);
    return state;
  }

private:
  std::shared_mutex _mutex;
  State state = State::Waiting;
  std::string id;
  std::shared_ptr<rtc::PeerConnection> peer_connection_;
};

}  // namespace hela::eillance
