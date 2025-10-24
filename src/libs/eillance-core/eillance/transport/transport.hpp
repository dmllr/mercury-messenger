#pragma once

#include "transport_delegate.hpp"


#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <rtc/datachannel.hpp>
#include <rtc/peerconnection.hpp>

#include "eillance/events/event_source.h"
#include "eillance/transport/peer.hpp"

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
  auto getPeerConnectionState(const std::string& peer_id) const -> ::rtc::PeerConnection::State { return self_->getPeerConnectionState(peer_id); }
  void removePeer(const std::string& peer_id) const { self_->removePeer(peer_id); }
  void sendSignalingMessage(const Json& message) const { self_->sendSignalingMessage(message); }
  void sendPeerDataChannelMessage(const std::shared_ptr<::rtc::DataChannel>& dc, const Json& message) const { self_->sendPeerDataChannelMessage(dc, message); }
  void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const { self_->addToStream(peer, isAddingVideo); }
  auto createPeerConnection(const std::string& peer_id, const std::string& context) const -> std::shared_ptr<::rtc::PeerConnection> { return self_->createPeerConnection(peer_id, context); }

  struct TransportInterface {
    TransportInterface() = default;
    TransportInterface(const TransportInterface&) = delete;
    TransportInterface(TransportInterface&&) noexcept = delete;
    TransportInterface& operator=(const TransportInterface&) = delete;
    TransportInterface& operator=(TransportInterface&&) noexcept = delete;
    virtual ~TransportInterface() = default;

    virtual void setDelegate(transport::TransportDelegate* delegate) = 0;
    virtual void setStatusCallback(const StatusCallbackFunction& callback) = 0;
    virtual void setName(const std::string& device_name) = 0;
    virtual void setId(const std::string& id) = 0;
    virtual void start() = 0;
    virtual void terminate() = 0;
    virtual void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) = 0;
    virtual auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> = 0;
    virtual auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> = 0;
    virtual auto getPeerConnectionState(const std::string& peer_id) const -> ::rtc::PeerConnection::State = 0;
    virtual void removePeer(const std::string& peer_id) = 0;
    virtual void sendSignalingMessage(const Json& message) const = 0;
    virtual void sendPeerDataChannelMessage(const std::shared_ptr<::rtc::DataChannel>& dc, const Json& message) const = 0;
    virtual void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const = 0;
    virtual auto createPeerConnection(const std::string& peer_id, const std::string& context) -> std::shared_ptr<::rtc::PeerConnection> = 0;
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
    auto getPeerConnectionState(const std::string& peer_id) const -> ::rtc::PeerConnection::State override { return transport_->getPeerConnectionState(peer_id); }
    void removePeer(const std::string& peer_id) override { transport_->removePeer(peer_id); }
    void sendSignalingMessage(const Json& message) const override { transport_->sendSignalingMessage(message); }
    void sendPeerDataChannelMessage(const std::shared_ptr<::rtc::DataChannel>& dc, const Json& message) const override { transport_->sendPeerDataChannelMessage(dc, message); }
    void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override { transport_->addToStream(peer, isAddingVideo); }
    auto createPeerConnection(const std::string& peer_id, const std::string& context) -> std::shared_ptr<::rtc::PeerConnection> override { return transport_->createPeerConnection(peer_id, context); }
  };

private:
  std::unique_ptr<TransportInterface> self_;
};

} // namespace eillance::transport
