#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/event_source.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/transport/track.hpp"
#include "eillance/transport/transport_delegate.hpp"

namespace eillance::transport {

class DataTransport {
 public:
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(ContactID const& peer_id, EventSource, int)>;

  struct Interface {
    virtual ~Interface() = default;
    virtual void SetDelegate(TransportDelegate* delegate) = 0;
    virtual void SetStatusCallback(StatusCallbackFunction const& callback) = 0;

    // Config
    virtual void SetId(ContactID const& id) = 0;
    virtual void SetSignalingUrl(std::string const& url) = 0;
    virtual void SetIceServers(std::string const& json_config) = 0;

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

    virtual auto SendData(ContactID const& peer_id, std::span<std::byte const> message) const -> bool = 0;
    virtual auto SendJson(ContactID const& peer_id, Json const& message) const -> bool = 0;
    virtual void SendSignalingMessage(Json const& message) const = 0;
  };

  template <typename T>
  explicit DataTransport(std::unique_ptr<T> impl)
      : self_(std::move(impl)) {
  }

  void SetDelegate(TransportDelegate* delegate) const {
    self_->SetDelegate(delegate);
  }
  void SetStatusCallback(StatusCallbackFunction const& callback) const {
    self_->SetStatusCallback(callback);
  }

  void SetId(ContactID const& id) const {
    self_->SetId(id);
  }
  void SetSignalingUrl(std::string const& url) const {
    self_->SetSignalingUrl(url);
  }
  void SetIceServers(std::string const& json_config) const {
    self_->SetIceServers(json_config);
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

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) const {
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
  auto SendJson(ContactID const& peer_id, Json const& message) const -> bool {
    return self_->SendJson(peer_id, message);
  }
  void SendSignalingMessage(Json const& message) const {
    self_->SendSignalingMessage(message);
  }

 private:
  std::unique_ptr<Interface> self_;
};

}  // namespace eillance::transport
