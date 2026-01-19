#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer.hpp"

namespace eillance::transport {

class TransportDelegate {
public:
  using Json = nlohmann::json;

  virtual ~TransportDelegate() = default;
  virtual void OnPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void OnSignalingErrorForPeer(const ContactID& peer_id, const std::string& error) = 0;
  virtual auto Sign(std::span<const std::byte> message) -> std::vector<std::byte> = 0;
  virtual void OnLoginSuccess() = 0;
  virtual void ProcessPeerMessage(Json message) = 0;
  virtual void OnSystemMessage(const std::string& type, const Json& message) = 0;
  virtual void OnDataChannelOpen(const ContactID& peer_id) = 0;
  virtual void OnFrameReceived(const ContactID& peer_id, const std::vector<std::byte>& frame) = 0;
};

}  // namespace eillance::transport
