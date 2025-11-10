#pragma once

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "eillance/crypto/crypto.hpp"
#include "eillance/transport/candidate.hpp"
#include "eillance/transport/track.hpp"
#include "eillance/utils/memory.h"

namespace eillance::transport {

struct Peer {
  enum class State {
    waiting,
    waitingForVideo,
    waitingForAudio,
    ready
  };

  explicit Peer(const std::string& id);

  std::shared_ptr<ITrack> video_;
  std::shared_ptr<ITrack> audio_;
  std::shared_ptr<void> data_peer_connection_;
  std::shared_ptr<void> media_peer_connection_;
  std::shared_ptr<void> data_channel_;
  std::atomic<bool> data_channel_is_open_{false};
  std::atomic<bool> has_media_peer_connection_{false};
  std::unique_ptr<crypto::SessionCipher> session_cipher_;
  std::function<void(const std::string&)> on_data_channel_message_;
  std::atomic<bool> incoming_call_notified_{false};
  std::optional<std::string> pending_offer_sdp;
  std::vector<Candidate> pending_candidates;

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

inline Peer::Peer(const std::string& id) : id_(id) { session_cipher_ = std::make_unique<crypto::SessionCipher>(); }

}  // namespace eillance::transport
