#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <rtc/track.hpp>

#include "eillance/transport/peer.hpp"

namespace eillance::transport {

class TransportDelegate {
public:
  using Json = nlohmann::json;

  virtual ~TransportDelegate() = default;
  virtual void onPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void onPeerOnline(const std::string& peer_id) = 0;
  virtual void onPeerOffline(const std::string& peer_id) = 0;
  virtual void onSignalingErrorForPeer(const std::string& peer_id, const std::string& error) = 0;
  virtual void processPeerMessage(Json message) = 0;
  virtual void processSystemMessage(const std::string& type, Json message) = 0;
  virtual void onTrack(const std::string& peer_id, std::shared_ptr<rtc::Track> track) = 0;
};

} // namespace eillance::transport
