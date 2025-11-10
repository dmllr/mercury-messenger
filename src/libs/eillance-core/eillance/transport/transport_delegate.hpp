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
  virtual void OnPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void OnSignalingErrorForPeer(const std::string& peer_id, const std::string& error) = 0;
  virtual auto Sign(const std::string& message) -> std::vector<std::byte> = 0;
  virtual void OnLoginSuccess() = 0;
  virtual void ProcessPeerMessage(Json message) = 0;
  virtual void OnSystemMessage(const std::string& type, const Json& message) = 0;
  virtual void OnDataChannelOpen(const std::string& peer_id) = 0;
  virtual void OnFrameReceived(const std::string& peer_id, const std::vector<std::byte>& frame) = 0;
};

}  // namespace eillance::transport
