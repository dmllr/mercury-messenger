#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.hpp"

namespace eillance::transport {

class DataTransport {
 public:
  using Json = nlohmann::json;

  struct Interface {
    virtual ~Interface() = default;

    // Config
    virtual void SetId(ContactID const& id) = 0;
    virtual void SetSecretKey(std::span<std::byte const> key) {
      (void)key;
    }
    virtual void SetBootstrapNodeUrls(std::vector<std::string> const& urls) = 0;

    // Lifecycle
    virtual void Start() = 0;
    virtual void Terminate() = 0;
    virtual void Reconnect() = 0;

    // Peer Management
    virtual void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) = 0;
    virtual void RemovePeer(ContactID const& peer_id) = 0;
    [[nodiscard]] virtual auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> = 0;
    [[nodiscard]] virtual auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> = 0;
    [[nodiscard]] virtual auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState = 0;

    // Actions
    virtual void Connect(ContactID const& peer_id) = 0;  // Create PeerConnection + DataChannel
    virtual void Disconnect(ContactID const& peer_id) = 0;

    virtual void CreateOffer(ContactID const& peer_id) = 0;
    virtual void CreateAnswer(ContactID const& peer_id) = 0;
    virtual void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) = 0;

    virtual auto SendData(ContactID const& peer_id, std::span<std::byte const> message) -> bool = 0;
    virtual auto SendJson(ContactID const& peer_id, Json const& message) -> bool = 0;

    virtual void DiscoverTurnRelays() = 0;
    virtual void AuthTurnRelay(ContactID const& peer_id) = 0;
    virtual void SetTurnEligibility(bool eligible) = 0;
  };

  template <typename T>
  explicit DataTransport(std::unique_ptr<T> impl)
      : self_(std::move(impl)) {
  }

  void SetId(ContactID const& id) const {
    self_->SetId(id);
  }
  void SetSecretKey(std::span<std::byte const> key) const {
    self_->SetSecretKey(key);
  }
  void SetBootstrapNodeUrls(std::vector<std::string> const& urls) const {
    self_->SetBootstrapNodeUrls(urls);
  }

  void Start() const {
    self_->Start();
  }
  void Terminate() const {
    self_->Terminate();
  }
  void Reconnect() const {
    self_->Reconnect();
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) {
    self_->AddPeer(peer_id, peer);
  }
  void RemovePeer(ContactID const& peer_id) const {
    self_->RemovePeer(peer_id);
  }
  [[nodiscard]] auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> {
    return self_->GetPeer(peer_id);
  }
  [[nodiscard]] auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> {
    return self_->GetPeers();
  }
  [[nodiscard]] auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState {
    return self_->GetConnectionState(peer_id);
  }

  void Connect(ContactID const& peer_id) const {
    self_->Connect(peer_id);
  }
  void Disconnect(ContactID const& peer_id) const {
    self_->Disconnect(peer_id);
  }

  void CreateOffer(ContactID const& peer_id) const {
    self_->CreateOffer(peer_id);
  }
  void CreateAnswer(ContactID const& peer_id) const {
    self_->CreateAnswer(peer_id);
  }
  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) const {
    self_->SetRemoteDescription(peer_id, sdp, type);
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) const -> bool {
    return self_->SendData(peer_id, message);
  }
  auto SendJson(ContactID const& peer_id, Json const& message) -> bool {
    return self_->SendJson(peer_id, message);
  }

  void DiscoverTurnRelays() const {
    self_->DiscoverTurnRelays();
  }
  void AuthTurnRelay(ContactID const& peer_id) const {
    self_->AuthTurnRelay(peer_id);
  }
  void SetTurnEligibility(bool eligible) const {
    self_->SetTurnEligibility(eligible);
  }

 private:
  std::unique_ptr<Interface> self_;
};

}  // namespace eillance::transport
