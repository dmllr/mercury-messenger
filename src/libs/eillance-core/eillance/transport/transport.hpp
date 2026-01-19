#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "eillance/events/event_source.h"
#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/track.hpp"
#include "eillance/transport/transport_delegate.hpp"

namespace eillance::transport {

class Transport {
public:
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(const ContactID& peer_id, EventSource, int)>;

  struct TransportInterface;

  template<typename T>
  class TransportModel;

  template<typename T>
  explicit Transport(std::unique_ptr<T> impl) : self_(new TransportModel<T>(std::move(impl))) {}

  Transport(const Transport&) = delete;
  Transport(Transport&&) noexcept = default;
  auto operator=(const Transport&) -> Transport& = delete;
  auto operator=(Transport&&) noexcept -> Transport& = default;
  ~Transport() = default;

  void SetDelegate(transport::TransportDelegate* delegate) const { self_->SetDelegate(delegate); }
  void SetStatusCallback(const StatusCallbackFunction& callback) const { self_->SetStatusCallback(callback); }
  void SetName(const std::string& device_name) const { self_->SetName(device_name); }
  void SetId(const ContactID& id) const { self_->SetId(id); }
  void SetSignalingUrl(const std::string& url) const { self_->SetSignalingUrl(url); }
  void Start() const { self_->Start(); }
  void Terminate() const { self_->Terminate(); }
  void Reconnect() const { self_->Reconnect(); }
  void AddPeer(const ContactID& peerId, const std::shared_ptr<Peer>& peer) const { self_->AddPeer(peerId, peer); }
  auto GetPeer(const ContactID& peerId) const -> std::shared_ptr<Peer> { return self_->GetPeer(peerId); }
  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> { return self_->GetPeers(); }
  auto GetPeerConnectionState(const ContactID& peerId) const -> eillance::transport::PeerConnectionState { return self_->GetPeerConnectionState(peerId); }
  void CreateDataPeerConnection(const ContactID& peerId) const { self_->CreateDataPeerConnection(peerId); }
  void CreateMediaPeerConnection(const ContactID& peerId) const { self_->CreateMediaPeerConnection(peerId); }
  void CreateDataChannel(const ContactID& peerId) const { self_->CreateDataChannel(peerId); }
  void ClosePeerConnection(const ContactID& peerId, const std::string& context) const { self_->ClosePeerConnection(peerId, context); }
  void CreateOffer(const ContactID& peerId, const std::string& context) const { self_->CreateOffer(peerId, context); }
  void CreateAnswer(const ContactID& peerId, const std::string& context) const { self_->CreateAnswer(peerId, context); }
  void SetRemoteDescription(const ContactID& peerId, const std::string& context, const std::string& sdp, const std::string& type) const { self_->SetRemoteDescription(peerId, context, sdp, type); }
  void AddRemoteCandidate(const ContactID& peerId, const std::string& context, const std::string& candidate, const std::string& mid) const { self_->AddRemoteCandidate(peerId, context, candidate, mid); }
  void RemovePeer(const ContactID& peerId) const { self_->RemovePeer(peerId); }
  void SendSignalingMessage(const Json& message) const { self_->SendSignalingMessage(message); }
  void SendPeerDataChannelMessage(const ContactID& peerId, const Json& message) const { self_->SendPeerDataChannelMessage(peerId, message); }
  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const { self_->AddToStream(peer, isAddingVideo); }
  auto AddVideo(const ContactID& peerId, const std::function<void(void)>& onOpen) const -> std::shared_ptr<ITrack> { return self_->AddVideo(peerId, onOpen); }
  auto AddAudio(const ContactID& peerId, const std::function<void(void)>& onOpen) const -> std::shared_ptr<ITrack> { return self_->AddAudio(peerId, onOpen); }

  struct TransportInterface {
    TransportInterface() = default;
    TransportInterface(const TransportInterface&) = delete;
    TransportInterface(TransportInterface&&) noexcept = delete;
    auto operator=(const TransportInterface&) -> TransportInterface& = delete;
    auto operator=(TransportInterface&&) noexcept -> TransportInterface& = delete;
    virtual ~TransportInterface() = default;

    virtual void SetDelegate(transport::TransportDelegate* delegate) = 0;
    virtual void SetStatusCallback(const StatusCallbackFunction& callback) = 0;
    virtual void SetName(const std::string& device_name) = 0;
    virtual void SetId(const ContactID& id) = 0;
    virtual void SetSignalingUrl(const std::string& url) = 0;
    virtual void Start() = 0;
    virtual void Terminate() = 0;
    virtual void Reconnect() = 0;

    virtual void AddPeer(const ContactID& peer_id, const std::shared_ptr<Peer>& peer) = 0;
    virtual auto GetPeer(const ContactID& peer_id) const -> std::shared_ptr<Peer> = 0;
    virtual auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> = 0;
    virtual void RemovePeer(const ContactID& peer_id) = 0;
    virtual auto GetPeerConnectionState(const ContactID& peer_id) const -> eillance::transport::PeerConnectionState = 0;

    virtual void CreateDataPeerConnection(const ContactID& peer_id) = 0;
    virtual void CreateMediaPeerConnection(const ContactID& peer_id) = 0;
    virtual void ClosePeerConnection(const ContactID& peer_id, const std::string& context) = 0;
    virtual void CreateOffer(const ContactID& peer_id, const std::string& context) = 0;
    virtual void CreateAnswer(const ContactID& peer_id, const std::string& context) = 0;
    virtual void SetRemoteDescription(const ContactID& peer_id, const std::string& context, const std::string& sdp, const std::string& type) = 0;
    virtual void AddRemoteCandidate(const ContactID& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) = 0;

    virtual void CreateDataChannel(const ContactID& peer_id) = 0;
    virtual void SendPeerDataChannelMessage(const ContactID& peer_id, const Json& message) const = 0;

    virtual void SendSignalingMessage(const Json& message) const = 0;

    virtual void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const = 0;
    virtual auto AddVideo(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> = 0;
    virtual auto AddAudio(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> = 0;
  };

  template<typename T>
  class TransportModel final : public TransportInterface {
    std::unique_ptr<T> transport_;

  public:
    explicit TransportModel(std::unique_ptr<T> impl) : transport_(std::move(impl)) {}

    void SetDelegate(transport::TransportDelegate* delegate) override { transport_->SetDelegate(delegate); }
    void SetStatusCallback(const StatusCallbackFunction& callback) override { transport_->SetStatusCallback(callback); }
    void SetName(const std::string& device_name) override { transport_->SetName(device_name); }
    void SetId(const ContactID& id) override { transport_->SetId(id); }
    void SetSignalingUrl(const std::string& url) override { transport_->SetSignalingUrl(url); }
    void Start() override { transport_->Start(); }
    void Terminate() override { transport_->Terminate(); }
    void Reconnect() override { transport_->Reconnect(); }
    void AddPeer(const ContactID& peer_id, const std::shared_ptr<Peer>& peer) override { transport_->AddPeer(peer_id, peer); }
    auto GetPeer(const ContactID& peer_id) const -> std::shared_ptr<Peer> override { return transport_->GetPeer(peer_id); }
    auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override { return transport_->GetPeers(); }
    auto GetPeerConnectionState(const ContactID& peer_id) const -> PeerConnectionState override { return transport_->GetPeerConnectionState(peer_id); }
    void CreateDataPeerConnection(const ContactID& peer_id) override { transport_->CreateDataPeerConnection(peer_id); }
    void CreateMediaPeerConnection(const ContactID& peer_id) override { transport_->CreateMediaPeerConnection(peer_id); }
    void CreateDataChannel(const ContactID& peer_id) override { transport_->CreateDataChannel(peer_id); }
    void ClosePeerConnection(const ContactID& peer_id, const std::string& context) override { transport_->ClosePeerConnection(peer_id, context); }
    void CreateOffer(const ContactID& peer_id, const std::string& context) override { transport_->CreateOffer(peer_id, context); }
    void CreateAnswer(const ContactID& peer_id, const std::string& context) override { transport_->CreateAnswer(peer_id, context); }
    void SetRemoteDescription(const ContactID& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override { transport_->SetRemoteDescription(peer_id, context, sdp, type); }
    void AddRemoteCandidate(const ContactID& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override { transport_->AddRemoteCandidate(peer_id, context, candidate, mid); }
    void RemovePeer(const ContactID& peer_id) override { transport_->RemovePeer(peer_id); }
    void SendSignalingMessage(const Json& message) const override { transport_->SendSignalingMessage(message); }
    void SendPeerDataChannelMessage(const ContactID& peer_id, const Json& message) const override { transport_->SendPeerDataChannelMessage(peer_id, message); }
    void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override { transport_->AddToStream(peer, isAddingVideo); }
    auto AddVideo(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return transport_->AddVideo(peer_id, on_open); }
    auto AddAudio(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return transport_->AddAudio(peer_id, on_open); }
  };

private:
  std::unique_ptr<TransportInterface> self_;
};

}  // namespace eillance::transport
