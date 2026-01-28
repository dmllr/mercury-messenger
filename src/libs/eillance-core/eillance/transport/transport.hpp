#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/event_source.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/transport/track.hpp"
#include "eillance/transport/transport_delegate.hpp"

namespace eillance::transport {

class Transport {
 public:
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(ContactID const& peer_id, EventSource, int)>;

  struct TransportInterface;

  template <typename T>
  class TransportModel;

  template <typename T>
  explicit Transport(std::unique_ptr<T> impl)
      : self_(new TransportModel<T>(std::move(impl))) {}

  Transport(Transport const&) = delete;
  Transport(Transport&&) noexcept = default;
  auto operator=(Transport const&) -> Transport& = delete;
  auto operator=(Transport&&) noexcept -> Transport& = default;
  ~Transport() = default;

  void SetDelegate(transport::TransportDelegate* delegate) const { self_->SetDelegate(delegate); }
  void SetStatusCallback(StatusCallbackFunction const& callback) const { self_->SetStatusCallback(callback); }
  void SetName(std::string const& device_name) const { self_->SetName(device_name); }
  void SetId(ContactID const& id) const { self_->SetId(id); }
  void SetSignalingUrl(std::string const& url) const { self_->SetSignalingUrl(url); }
  void Start() const { self_->Start(); }
  void Terminate() const { self_->Terminate(); }
  void Reconnect() const { self_->Reconnect(); }
  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) const { self_->AddPeer(peer_id, peer); }
  [[nodiscard]] auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> {
    return self_->GetPeer(peer_id);
  }
  [[nodiscard]] auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> {
    return self_->GetPeers();
  }
  [[nodiscard]] auto GetPeerConnectionState(ContactID const& peer_id
  ) const -> eillance::transport::PeerConnectionState {
    return self_->GetPeerConnectionState(peer_id);
  }
  void CreateDataPeerConnection(ContactID const& peer_id) const { self_->CreateDataPeerConnection(peer_id); }
  void CreateMediaPeerConnection(ContactID const& peer_id) const { self_->CreateMediaPeerConnection(peer_id); }
  void CreateDataChannel(ContactID const& peer_id) const { self_->CreateDataChannel(peer_id); }
  void ClosePeerConnection(ContactID const& peer_id, std::string const& context) const {
    self_->ClosePeerConnection(peer_id, context);
  }
  void CreateOffer(ContactID const& peer_id, std::string const& context) const { self_->CreateOffer(peer_id, context); }
  void CreateAnswer(ContactID const& peer_id, std::string const& context) const {
    self_->CreateAnswer(peer_id, context);
  }
  void SetRemoteDescription(
      ContactID const& peer_id,
      std::string const& context,
      std::string const& sdp,
      std::string const& type
  ) const {
    self_->SetRemoteDescription(peer_id, context, sdp, type);
  }
  void AddRemoteCandidate(
      ContactID const& peer_id,
      std::string const& context,
      std::string const& candidate,
      std::string const& mid
  ) const {
    self_->AddRemoteCandidate(peer_id, context, candidate, mid);
  }
  void RemovePeer(ContactID const& peer_id) const { self_->RemovePeer(peer_id); }
  void SendSignalingMessage(Json const& message) const { self_->SendSignalingMessage(message); }
  void SendPeerDataChannelMessage(ContactID const& peer_id, Json const& message) const {
    self_->SendPeerDataChannelMessage(peer_id, message);
  }
  auto SendPeerDataChannelBinaryMessage(ContactID const& peer_id, std::span<std::byte const> message) const -> bool {
    return self_->SendPeerDataChannelBinaryMessage(peer_id, message);
  }
  void AddToStream(std::shared_ptr<Peer> const& peer, bool is_adding_video) const {
    self_->AddToStream(peer, is_adding_video);
  }
  auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open) const -> std::shared_ptr<ITrack> {
    return self_->AddVideo(peer_id, on_open);
  }
  auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open) const -> std::shared_ptr<ITrack> {
    return self_->AddAudio(peer_id, on_open);
  }

  struct TransportInterface {
    TransportInterface() = default;
    TransportInterface(TransportInterface const&) = delete;
    TransportInterface(TransportInterface&&) noexcept = delete;
    auto operator=(TransportInterface const&) -> TransportInterface& = delete;
    auto operator=(TransportInterface&&) noexcept -> TransportInterface& = delete;
    virtual ~TransportInterface() = default;

    virtual void SetDelegate(transport::TransportDelegate* delegate) = 0;
    virtual void SetStatusCallback(StatusCallbackFunction const& callback) = 0;
    virtual void SetName(std::string const& device_name) = 0;
    virtual void SetId(ContactID const& id) = 0;
    virtual void SetSignalingUrl(std::string const& url) = 0;
    virtual void Start() = 0;
    virtual void Terminate() = 0;
    virtual void Reconnect() = 0;

    virtual void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) = 0;
    [[nodiscard]] virtual auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> = 0;
    [[nodiscard]] virtual auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> = 0;
    virtual void RemovePeer(ContactID const& peer_id) = 0;
    [[nodiscard]] virtual auto GetPeerConnectionState(ContactID const& peer_id
    ) const -> eillance::transport::PeerConnectionState = 0;

    virtual void CreateDataPeerConnection(ContactID const& peer_id) = 0;
    virtual void CreateMediaPeerConnection(ContactID const& peer_id) = 0;
    virtual void ClosePeerConnection(ContactID const& peer_id, std::string const& context) = 0;
    virtual void CreateOffer(ContactID const& peer_id, std::string const& context) = 0;
    virtual void CreateAnswer(ContactID const& peer_id, std::string const& context) = 0;
    virtual void SetRemoteDescription(
        ContactID const& peer_id,
        std::string const& context,
        std::string const& sdp,
        std::string const& type
    ) = 0;
    virtual void AddRemoteCandidate(
        ContactID const& peer_id,
        std::string const& context,
        std::string const& candidate,
        std::string const& mid
    ) = 0;

    virtual void CreateDataChannel(ContactID const& peer_id) = 0;
    virtual void SendPeerDataChannelMessage(ContactID const& peer_id, Json const& message) const = 0;

    virtual auto SendPeerDataChannelBinaryMessage(ContactID const& peer_id, std::span<std::byte const> message) const
        -> bool = 0;

    virtual void SendSignalingMessage(Json const& message) const = 0;

    virtual void AddToStream(std::shared_ptr<Peer> const& peer, bool is_adding_video) const = 0;
    virtual auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> = 0;
    virtual auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> = 0;
  };

  template <typename T>
  class TransportModel final : public TransportInterface {
    std::unique_ptr<T> transport_{};

   public:
    explicit TransportModel(std::unique_ptr<T> impl)
        : transport_(std::move(impl)) {}

    void SetDelegate(transport::TransportDelegate* delegate) override { transport_->SetDelegate(delegate); }
    void SetStatusCallback(StatusCallbackFunction const& callback) override { transport_->SetStatusCallback(callback); }
    void SetName(std::string const& device_name) override { transport_->SetName(device_name); }
    void SetId(ContactID const& id) override { transport_->SetId(id); }
    void SetSignalingUrl(std::string const& url) override { transport_->SetSignalingUrl(url); }
    void Start() override { transport_->Start(); }
    void Terminate() override { transport_->Terminate(); }
    void Reconnect() override { transport_->Reconnect(); }
    void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
      transport_->AddPeer(peer_id, peer);
    }
    [[nodiscard]] auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
      return transport_->GetPeer(peer_id);
    }
    [[nodiscard]] auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
      return transport_->GetPeers();
    }
    [[nodiscard]] auto GetPeerConnectionState(ContactID const& peer_id) const -> PeerConnectionState override {
      return transport_->GetPeerConnectionState(peer_id);
    }
    void CreateDataPeerConnection(ContactID const& peer_id) override { transport_->CreateDataPeerConnection(peer_id); }
    void CreateMediaPeerConnection(ContactID const& peer_id) override {
      transport_->CreateMediaPeerConnection(peer_id);
    }
    void CreateDataChannel(ContactID const& peer_id) override { transport_->CreateDataChannel(peer_id); }
    void ClosePeerConnection(ContactID const& peer_id, std::string const& context) override {
      transport_->ClosePeerConnection(peer_id, context);
    }
    void CreateOffer(ContactID const& peer_id, std::string const& context) override {
      transport_->CreateOffer(peer_id, context);
    }
    void CreateAnswer(ContactID const& peer_id, std::string const& context) override {
      transport_->CreateAnswer(peer_id, context);
    }
    void SetRemoteDescription(
        ContactID const& peer_id,
        std::string const& context,
        std::string const& sdp,
        std::string const& type
    ) override {
      transport_->SetRemoteDescription(peer_id, context, sdp, type);
    }
    void AddRemoteCandidate(
        ContactID const& peer_id,
        std::string const& context,
        std::string const& candidate,
        std::string const& mid
    ) override {
      transport_->AddRemoteCandidate(peer_id, context, candidate, mid);
    }
    void RemovePeer(ContactID const& peer_id) override { transport_->RemovePeer(peer_id); }
    void SendSignalingMessage(Json const& message) const override { transport_->SendSignalingMessage(message); }
    void SendPeerDataChannelMessage(ContactID const& peer_id, Json const& message) const override {
      transport_->SendPeerDataChannelMessage(peer_id, message);
    }
    auto SendPeerDataChannelBinaryMessage(ContactID const& peer_id, std::span<std::byte const> message) const
        -> bool override {
      return transport_->SendPeerDataChannelBinaryMessage(peer_id, message);
    }
    void AddToStream(std::shared_ptr<Peer> const& peer, bool is_adding_video) const override {
      transport_->AddToStream(peer, is_adding_video);
    }
    auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> override {
      return transport_->AddVideo(peer_id, on_open);
    }
    auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> override {
      return transport_->AddAudio(peer_id, on_open);
    }
  };

 private:
  std::unique_ptr<TransportInterface> self_{};
};

}  // namespace eillance::transport
