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

 public:
  Libp2pDataTransport() = default;

  void SetId(ContactID const& id) override {
    id_ = id;
  }

  void SetSecretKey(std::span<std::byte const> key) override {
    secret_key_.assign(key.begin(), key.end());
  }

  void SetSignalingUrl(std::string const& url) override {
    bootstrap_url_ = url;
    if (node_ && !bootstrap_url_.empty()) {
      node_->AddBootstrapNode(bootstrap_url_);
    }
  }

  void Start() override {
    if (!node_) {
      node_ = std::make_shared<Libp2pNode>(secret_key_);

      node_->SetOnConnected([this](ContactID const& peer_id) {
        auto p = GetPeer(peer_id);
        if (!p) {
          p = std::make_shared<Peer>(peer_id);
          AddPeer(peer_id, p);
        }
        p->data_channel_is_open = true;
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

    if (!bootstrap_url_.empty()) {
      node_->AddBootstrapNode(bootstrap_url_);
    }

    node_->Start();
    EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});
  }

  void Terminate() override {
    if (node_) {
      node_->Stop();
    }
    EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::disconnected});
  }

  void Reconnect() override {
    if (node_) {
      node_->Stop();
      node_->Start();
    }
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
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connecting});
      node_->Dial(peer_id);
    }
  }

  void Disconnect(ContactID const& peer_id) override {
    if (auto p = GetPeer(peer_id)) {
      p->data_channel_is_open = false;
    }
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
    std::span<std::byte const> bytes(reinterpret_cast<std::byte const*>(str.data()), str.size());
    return SendData(peer_id, bytes);
  }

  void SendSignalingMessage(Json const& message) const override {
    std::string to = message.value("to", "");
    if (!to.empty() && node_) {
      try {
        ContactID peer_id(to);
        auto str = message.dump();
        std::span<std::byte const> bytes(reinterpret_cast<std::byte const*>(str.data()), str.size());
        node_->SendMessage(peer_id, bytes);
      } catch (std::exception const& e) {
        LGE << "Invalid ContactID in signaling message target: " << e.what();
      }
    }
  }
};

}  // namespace eillance::transport
