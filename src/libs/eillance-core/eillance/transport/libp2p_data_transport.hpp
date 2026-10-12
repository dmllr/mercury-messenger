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
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::transport {

class Libp2pDataTransport : public DataTransport::Interface {
  using Json = nlohmann::json;

  DispatchQueue rx_queue_{"Libp2pRx", 1};
  std::shared_ptr<Libp2pNode> node_;
  ContactID id_;
  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;
  std::vector<std::byte> secret_key_;
  std::string bootstrap_url_;
  bool is_started_ = false;
  bool is_reconnecting_ = false;

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

      node_->SetOnConnected([this](ContactID const& peer_id, bool is_circuit) -> void {
        rx_queue_.Dispatch([this, peer_id, is_circuit]() -> void {
          auto p = GetPeer(peer_id);
          if (!p) {
            p = std::make_shared<Peer>(peer_id);
            AddPeer(peer_id, p);
          }
          p->data_channel_is_open = true;
          p->is_connecting = false;
          p->is_circuit = is_circuit;
          LGI << "Peer connected: " << peer_id.ToString() << (is_circuit ? " (circuit)" : " (direct)");
          EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connected});
          EventQueue::Instance().Push(EventDataChannelOpen{peer_id});
        });
      });

      node_->SetOnDisconnected([this](ContactID const& peer_id) -> void {
        rx_queue_.Dispatch([this, peer_id]() -> void {
          if (auto p = GetPeer(peer_id)) {
            p->data_channel_is_open = false;
            p->is_connecting = false;
            p->data_channel_initialized = false;
          }
          EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::disconnected});
        });
      });

      node_->SetOnMessage([this](ContactID const& peer_id, std::span<std::byte const> data) -> void {
        std::vector<std::byte> payload(data.begin(), data.end());
        rx_queue_.Dispatch([peer_id, payload = std::move(payload)]() mutable -> void {
          EventQueue::Instance().Push(EventDataChannelMessage{peer_id, std::move(payload)});
        });
      });
    }

    node_->Start();

    if (!bootstrap_url_.empty()) {
      LGI << "Connecting to bootstrap: " << bootstrap_url_;
      node_->AddBootstrapNode(bootstrap_url_);
    } else {
      std::string const fallback_addr = "/ip4/127.0.0.1/tcp/10807";
      LGI << "Connecting to fallback bootstrap: " << fallback_addr;
      node_->AddBootstrapNode(fallback_addr);
    }

    node_->Listen("/ip4/0.0.0.0/tcp/0");
    node_->Listen("/ip6/::/tcp/0");

    if (!is_reconnecting_) {
      EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});
    }
  }

  void Terminate() override {
    if (!is_started_) return;
    is_started_ = false;
    if (node_) node_->Stop();
    if (!is_reconnecting_) {
      EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::disconnected});
    }
  }

  void Reconnect() override {
    if (!is_started_) return;
    is_reconnecting_ = true;
    Terminate();

    std::vector<ContactID> to_notify;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      for (auto& [id, p] : peers_) {
        if (p->data_channel_is_open || p->is_connecting) {
          p->data_channel_is_open = false;
          p->is_connecting = false;
          p->data_channel_initialized = false;
          to_notify.push_back(id);
        }
      }
    }
    for (auto const& id : to_notify) {
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{id, PeerConnectionState::disconnected});
    }

    Start();
    is_reconnecting_ = false;
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
    if (auto p = GetPeer(peer_id)) {
      if (p->data_channel_is_open) return PeerConnectionState::connected;
      if (p->is_connecting) return PeerConnectionState::connecting;
    }
    return PeerConnectionState::disconnected;
  }

  void Connect(ContactID const& peer_id) override {
    if (node_) {
      auto p = GetPeer(peer_id);
      if (!p) {
        p = std::make_shared<Peer>(peer_id);
        AddPeer(peer_id, p);
      }
      p->is_connecting = true;
      LGI << "Triggering automated DHT search for: " << peer_id.ToString();
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connecting});
      node_->Dial(peer_id);
    }
  }

  void Disconnect(ContactID const& peer_id) override {
    bool changed = false;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      auto it = peers_.find(peer_id);
      if (it != peers_.end()) {
        auto& p = it->second;
        if (p->data_channel_is_open || p->is_connecting) {
          p->data_channel_is_open = false;
          p->is_connecting = false;
          p->data_channel_initialized = false;
          changed = true;
        }
      }
    }
    if (node_) {
      node_->Disconnect(peer_id);
    }
    if (changed) {
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::disconnected});
    }
  }

  void CreateOffer(ContactID const&) override {
  }
  void CreateAnswer(ContactID const&) override {
  }
  void SetRemoteDescription(ContactID const&, std::string const&, std::string const&) override {
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) -> bool override {
    if (node_) return node_->SendFile(peer_id, message);
    return false;
  }

  auto SendJson(ContactID const& peer_id, Json const& message) -> bool override {
    auto str = message.dump();
    if (node_) return node_->SendChat(peer_id, {reinterpret_cast<std::byte const*>(str.data()), str.size()});
    return false;
  }

  void SendSignalingMessage(Json const&) const override {
  }
};

}  // namespace eillance::transport
