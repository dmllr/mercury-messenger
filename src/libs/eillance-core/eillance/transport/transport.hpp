#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "eillance/events/event_source.h"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/track.hpp"
#include "eillance/transport/transport_delegate.hpp"

namespace eillance::transport {

class Transport {
public:
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(const std::string& peer_id, EventSource, int)>;

  struct TransportInterface;

  template<typename T>
  class TransportModel;

  template<typename T>
  explicit Transport(std::unique_ptr<T> impl)
      : self_(new TransportModel<T>(std::move(impl))) {}

  Transport(const Transport&) = delete;
  Transport(Transport&&) noexcept = default;
  Transport& operator=(const Transport&) = delete;
  Transport& operator=(Transport&&) noexcept = default;
  ~Transport() = default;

  void setDelegate(transport::TransportDelegate* delegate) const { self_->setDelegate(delegate); }
  void setStatusCallback(const StatusCallbackFunction& callback) const { self_->setStatusCallback(callback); }
  void setName(const std::string& device_name) const { self_->setName(device_name); }
  void setId(const std::string& id) const { self_->setId(id); }
  void start() const { self_->start(); }
  void terminate() const { self_->terminate(); }
  void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) const { self_->addPeer(peer_id, peer); }
  auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> { return self_->getPeer(peer_id); }
  auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> { return self_->getPeers(); }
  auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState { return self_->getPeerConnectionState(peer_id); }
  void createDataPeerConnection(const std::string& peer_id) const { self_->createDataPeerConnection(peer_id); }
  void createMediaPeerConnection(const std::string& peer_id) const { self_->createMediaPeerConnection(peer_id); }
  void createDataChannel(const std::string& peer_id) const { self_->createDataChannel(peer_id); }
  void closePeerConnection(const std::string& peer_id, const std::string& context) const { self_->closePeerConnection(peer_id, context); }
  void createOffer(const std::string& peer_id, const std::string& context) const { self_->createOffer(peer_id, context); }
  void createAnswer(const std::string& peer_id, const std::string& context) const { self_->createAnswer(peer_id, context); }
  void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) const { self_->setRemoteDescription(peer_id, context, sdp, type); }
  void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) const { self_->addRemoteCandidate(peer_id, context, candidate, mid); }
  void removePeer(const std::string& peer_id) const { self_->removePeer(peer_id); }
  void sendSignalingMessage(const Json& message) const { self_->sendSignalingMessage(message); }
  void sendPeerDataChannelMessage(const std::string& peer_id, const Json& message) const { self_->sendPeerDataChannelMessage(peer_id, message); }
  void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const { self_->addToStream(peer, isAddingVideo); }
  auto addVideo(const std::string& peer_id, const std::function<void(void)>& on_open) const -> std::shared_ptr<ITrack> { return self_->addVideo(peer_id, on_open); }
  auto addAudio(const std::string& peer_id, const std::function<void(void)>& on_open) const -> std::shared_ptr<ITrack> { return self_->addAudio(peer_id, on_open); }

  struct TransportInterface {
    TransportInterface() = default;
    TransportInterface(const TransportInterface&) = delete;
    TransportInterface(TransportInterface&&) noexcept = delete;
    TransportInterface& operator=(const TransportInterface&) = delete;
    TransportInterface& operator=(TransportInterface&&) noexcept = delete;
    virtual ~TransportInterface() = default;

    // --- Core Lifecycle & Delegates ---
    virtual void setDelegate(transport::TransportDelegate* delegate) = 0; // Used by Android
    virtual void setStatusCallback(const StatusCallbackFunction& callback) = 0; // Used by Android
    virtual void setName(const std::string& device_name) = 0; // Not used by Android (name is set in ClientEngine)
    virtual void setId(const std::string& id) = 0; // Used by Android
    virtual void start() = 0; // Used by Android
    virtual void terminate() = 0; // Used by Android

    // --- Peer Management ---
    virtual void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) = 0; // Used by Android
    virtual auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> = 0; // Used by Android
    virtual auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> = 0; // Used by Android
    virtual void removePeer(const std::string& peer_id) = 0; // Used by Android
    virtual auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState = 0; // Used by Android

    // --- Peer Connection Signaling ---
    virtual void createDataPeerConnection(const std::string& peer_id) = 0; // Used by Android
    virtual void createMediaPeerConnection(const std::string& peer_id) = 0; // Used by Android
    virtual void closePeerConnection(const std::string& peer_id, const std::string& context) = 0; // Used by Android
    virtual void createOffer(const std::string& peer_id, const std::string& context) = 0; // Used by Android
    virtual void createAnswer(const std::string& peer_id, const std::string& context) = 0; // Used by Android
    virtual void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) = 0; // Used by Android
    virtual void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) = 0; // Used by Android

    // --- Data Channel Communication ---
    virtual void createDataChannel(const std::string& peer_id) = 0; // Used by Android
    virtual void sendPeerDataChannelMessage(const std::string& peer_id, const Json& message) const = 0; // Used by Android

    // --- Signaling Server Communication ---
    virtual void sendSignalingMessage(const Json& message) const = 0; // Used by Android

    // --- Manual Media Stream/Track Management ---
    // These methods are specific to the CLI's custom media handling and are NOT USED by the Android implementation.
    virtual void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const = 0; // NOT used by Android
    virtual auto addVideo(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> = 0; // Used by Android (returns a stub)
    virtual auto addAudio(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> = 0; // Used by Android (returns a stub)
  };

  template<typename T>
  class TransportModel final : public TransportInterface {
    std::unique_ptr<T> transport_;

  public:
    explicit TransportModel(std::unique_ptr<T> impl)
        : transport_(std::move(impl)) {}

    void setDelegate(transport::TransportDelegate* delegate) override { transport_->setDelegate(delegate); }
    void setStatusCallback(const StatusCallbackFunction& callback) override { transport_->setStatusCallback(callback); }
    void setName(const std::string& device_name) override { transport_->setName(device_name); }
    void setId(const std::string& id) override { transport_->setId(id); }
    void start() override { transport_->start(); }
    void terminate() override { transport_->terminate(); }
    void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) override { transport_->addPeer(peer_id, peer); }
    auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> override { return transport_->getPeer(peer_id); }
    auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> override { return transport_->getPeers(); }
    auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState override { return transport_->getPeerConnectionState(peer_id); }
    void createDataPeerConnection(const std::string& peer_id) override { transport_->createDataPeerConnection(peer_id); }
    void createMediaPeerConnection(const std::string& peer_id) override { transport_->createMediaPeerConnection(peer_id); }
    void createDataChannel(const std::string& peer_id) override { transport_->createDataChannel(peer_id); }
    void closePeerConnection(const std::string& peer_id, const std::string& context) override { transport_->closePeerConnection(peer_id, context); }
    void createOffer(const std::string& peer_id, const std::string& context) override { transport_->createOffer(peer_id, context); }
    void createAnswer(const std::string& peer_id, const std::string& context) override { transport_->createAnswer(peer_id, context); }
    void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override { transport_->setRemoteDescription(peer_id, context, sdp, type); }
    void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override { transport_->addRemoteCandidate(peer_id, context, candidate, mid); }
    void removePeer(const std::string& peer_id) override { transport_->removePeer(peer_id); }
    void sendSignalingMessage(const Json& message) const override { transport_->sendSignalingMessage(message); }
    void sendPeerDataChannelMessage(const std::string& peer_id, const Json& message) const override { transport_->sendPeerDataChannelMessage(peer_id, message); }
    void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override { transport_->addToStream(peer, isAddingVideo); }
    auto addVideo(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return transport_->addVideo(peer_id, on_open); }
    auto addAudio(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<ITrack> override { return transport_->addAudio(peer_id, on_open); }
  };

private:
  std::unique_ptr<TransportInterface> self_;
};

} // namespace eillance::transport
