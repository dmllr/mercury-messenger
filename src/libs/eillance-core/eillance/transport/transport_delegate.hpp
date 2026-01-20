#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer.hpp"

namespace eillance::transport {

class TransportDelegate {
 public:
  using Json = nlohmann::json;

  virtual ~TransportDelegate() = default;
  virtual void OnPeerReady(std::shared_ptr<Peer> const& peer) = 0;
  virtual void OnSignalingErrorForPeer(ContactID const& peer_id, std::string const& error) = 0;
  virtual auto Sign(std::span<std::byte const> message) -> std::vector<std::byte> = 0;
  virtual void OnLoginSuccess() = 0;
  virtual void ProcessPeerMessage(Json message) = 0;
  virtual void OnSystemMessage(std::string const& type, Json const& message) = 0;
  virtual void OnDataChannelOpen(ContactID const& peer_id) = 0;
  virtual void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) = 0;
};

}  // namespace eillance::transport
