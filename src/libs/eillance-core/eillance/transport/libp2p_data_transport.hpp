#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/libp2p_node.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::transport {

class Libp2pDataTransport : public DataTransport::Interface {
  using Json = nlohmann::json;

  std::shared_ptr<Libp2pNode> node_;
  ContactID id_;
  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;
  std::vector<std::byte> secret_key_;
  std::string bootstrap_url_;
  bool is_started_ = false;

 public:
  Libp2pDataTransport() = default;

  void SetId(ContactID const& id) override {
    id_ = id;
  }

  void SetSecretKey(std::span<std::byte const> key) override {
    secret_key_.assign(key.begin(), key.end());
  }

  void SetBootstrapNodeUrl(std::string const& url) override {
    bootstrap_url_ = url;
  }

  void Start() override {
    if (is_started_) return;
    is_started_ = true;

    if (!node_) {
      node_ = std::make_shared<Libp2pNode>(secret_key_);

      node_->SetOnConnected([this](ContactID const& peer_id) {
          auto p = GetPeer(peer_id);
          if (!p) {
              p = std::make_shared<Peer>(peer_id);
              AddPeer(peer_id, p);
          }
          p->data_channel_is_open = true;
          LGI << "Peer connected: " << peer_id.ToString();
          EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connected});
          EventQueue::Instance().Push(EventDataChannelOpen{peer_id});
      });

      node_->SetOnDisconnected([this](ContactID const& peer_id) {
        if (auto p = GetPeer(peer_id)) {
          p->data_channel_is_open = false;
        }
        EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::disconnected});
      });

      node_->SetOnMessage([this](ContactID const& peer_id, std::span<std::byte const> data) {
        std::vector<std::byte> payload(data.begin(), data.end());
        EventQueue::Instance().Push(EventDataChannelMessage{peer_id, std::move(payload)});
      });
    }

    node_->Start();

    if (!bootstrap_url_.empty()) {
      LGI << "Connecting to bootstrap: " << bootstrap_url_;
      node_->AddBootstrapNode(bootstrap_url_);
    } else {
      std::string const fallback_addr = "/ip4/127.0.0.1/tcp/807";
      LGI << "Connecting to fallback bootstrap: " << fallback_addr;
      node_->AddBootstrapNode(fallback_addr);
    }

    node_->Listen("/ip4/0.0.0.0/tcp/0");
    node_->Listen("/ip6/::/tcp/0");

    EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});
  }

  void Terminate() override {
    if (!is_started_) return;
    is_started_ = false;
    if (node_) node_->Stop();
    EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::disconnected});
  }

  void Reconnect() override {
    if (!is_started_) return;
    Terminate();
    Start();
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_[peer_id] = peer;
  }

  void RemovePeer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.erase(peer_id);
  }

  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) return it->second;
    return nullptr;
  }

  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    return peers_;
  }

  auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState override {
    if (auto p = GetPeer(peer_id); p && p->data_channel_is_open) {
      return PeerConnectionState::connected;
    }
    return PeerConnectionState::disconnected;
  }

  void Connect(ContactID const& peer_id) override {
    if (node_) {
      LGI << "Triggering automated DHT search for: " << peer_id.ToString();
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connecting});
      node_->Dial(peer_id);
    }
  }

  void Disconnect(ContactID const& peer_id) override {
    if (auto p = GetPeer(peer_id)) p->data_channel_is_open = false;
    EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::disconnected});
  }

  void CreateOffer(ContactID const&) override {}
  void CreateAnswer(ContactID const&) override {}
  void SetRemoteDescription(ContactID const&, std::string const&, std::string const&) override {}

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) -> bool override {
    if (node_) return node_->SendMessage(peer_id, message);
    return false;
  }

  auto SendJson(ContactID const& peer_id, Json const& message) -> bool override {
    auto str = message.dump();
    return SendData(peer_id, {reinterpret_cast<std::byte const*>(str.data()), str.size()});
  }

  void SendSignalingMessage(Json const&) const override {}
};

}  // namespace eillance::transport
