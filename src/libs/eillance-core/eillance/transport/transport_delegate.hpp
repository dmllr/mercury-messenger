#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/transport/peer.hpp"

namespace eillance::transport {

class TransportDelegate {
public:
  using Json = nlohmann::json;

  virtual ~TransportDelegate() = default;
  virtual void onPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void onSignalingErrorForPeer(const std::string& peer_id, const std::string& error) = 0;
  virtual auto Sign(const std::string& message) -> std::vector<std::byte> = 0;
  virtual void onLoginSuccess() = 0;
  virtual void processPeerMessage(Json message) = 0;
  virtual void processSystemMessage(const std::string& type, Json message) = 0;
  virtual void onFrameReceived(const std::string& peer_id, const std::vector<std::byte>& frame) = 0;
};

}  // namespace eillance::transport
