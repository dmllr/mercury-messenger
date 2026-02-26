#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <span>

#include "eillance/transport/data_transport.hpp"
#include "mock_call_log.hpp"

namespace eillance::transport {

class MockDataTransport : public DataTransport::Interface {
  using Json = nlohmann::json;
  std::shared_ptr<MockCallLog> log_;
  mutable std::mutex mutex_;

 public:
  explicit MockDataTransport(std::shared_ptr<MockCallLog> log)
      : log_(std::move(log)) {
  }

  TransportDelegate* delegate = nullptr;
  std::map<ContactID, std::shared_ptr<Peer>> peers;

  void SetDelegate(TransportDelegate* d) override {
    delegate = d;
  }
  void SetName(std::string const&) {
  }  // No-op/Removed from interface but kept for mock compatibility if needed
  void SetId(ContactID const&) override {
  }
  void SetSignalingUrl(std::string const& url) override {
    log_->LogSignalingUrl(url);
  }
  void SetIceServers(std::string const&) override {
  }

  void Start() override {
    log_->LogStart();
  }
  void Terminate() override {
    log_->LogTerminate();
  }
  void Reconnect() override {
    log_->LogReconnect();
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(mutex_);
    peers[peer_id] = peer;
  }
  void RemovePeer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(mutex_);
    peers.erase(peer_id);
  }
  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto it = peers.find(peer_id); it != peers.end()) return it->second;
    return nullptr;
  }
  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::unordered_map<ContactID, std::shared_ptr<Peer>> result;
    for (auto const& [k, v] : peers) result[k] = v;
    return result;
  }
  auto GetConnectionState(ContactID const&) const -> PeerConnectionState override {
    return PeerConnectionState::connected;
  }

  void Connect(ContactID const&) override {
  }
  void Disconnect(ContactID const& peer_id) override {
    log_->LogClose(peer_id, "data");
  }

  void CreateOffer(ContactID const& peer_id) override {
    log_->LogOffer(peer_id, "data");
  }
  void CreateAnswer(ContactID const& peer_id) override {
    log_->LogAnswer(peer_id, "data");
  }
  void SetRemoteDescription(ContactID const&, std::string const&, std::string const&) override {
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) const -> bool override {
    log_->LogBinaryMsg(peer_id, {message.begin(), message.end()});
    return true;
  }
  auto SendJson(ContactID const& peer_id, Json const& message) const -> bool override {
    log_->LogPeerMsg(peer_id, message);
    return true;
  }
  void SendSignalingMessage(Json const& message) const override {
    log_->LogSignaling(message);
  }

  // Helpers for tests
  void SimulatePeerMessage(ContactID const& peer_id, Json const& message) {
    if (delegate) delegate->ProcessPeerMessage(message);
  }

  auto CallLog() const -> std::shared_ptr<MockCallLog> {
    return log_;
  }
};

}  // namespace eillance::transport
