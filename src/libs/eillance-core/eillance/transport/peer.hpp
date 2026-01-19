#pragma once

#include <atomic>
#include <optional>
#include <shared_mutex>
#include <vector>

#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/transport/candidate.hpp"
#include "eillance/transport/track.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::transport {

struct Peer {
  enum class State {
    waiting,
    waitingForVideo,
    waitingForAudio,
    ready
  };

  explicit Peer(const ContactID& id);

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  const ContactID id;

  std::shared_ptr<ITrack> video;
  std::shared_ptr<ITrack> audio;
  std::shared_ptr<void> data_peer_connection;
  std::shared_ptr<void> media_peer_connection;
  std::shared_ptr<void> data_channel;
  std::atomic<bool> data_channel_is_open{false};
  std::atomic<Time::Unit> last_activity_time{0};
  std::atomic<bool> has_media_peer_connection{false};
  std::unique_ptr<crypto::SessionCipher> session_cipher;
  std::function<void(const std::string&)> on_data_channel_message;
  std::atomic<bool> incoming_call_notified{false};
  std::optional<std::string> pending_offer_sdp;
  std::vector<Candidate> pending_candidates;
  // NOLINTEND(*-non-private-member-variables-in-classes)

  void SetState(State state) {
    const std::unique_lock<std::shared_mutex> lock(mutex_);
    this->state_ = state;
  }

  auto GetState() -> State {
    const std::shared_lock<std::shared_mutex> lock(mutex_);
    return state_;
  }

private:
  std::shared_mutex mutex_;
  State state_ = State::waiting;
};

inline Peer::Peer(const ContactID& id) : id(id), last_activity_time(Time::Now()) {
  session_cipher = std::make_unique<crypto::SessionCipher>();
}

}  // namespace eillance::transport
