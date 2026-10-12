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

  NodeRole role_;
  DispatchQueue rx_queue_{"Libp2pRx", 1};
  std::shared_ptr<Libp2pNode> node_;
  ContactID master_id_;
  ContactID device_id_;
  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;
  std::unordered_map<ContactID, std::vector<ContactID>> master_to_devices_;
  std::unordered_map<ContactID, ContactID> device_to_master_;
  std::vector<std::byte> device_secret_key_;
  std::vector<std::string> bootstrap_urls_;
  bool is_started_ = false;
  bool is_reconnecting_ = false;

 public:
  explicit Libp2pDataTransport(NodeRole role = NodeRole::desktopClient)
      : role_(role) {
  }

  void SetId(ContactID const& id) override {
    master_id_ = id;
  }

  void SetDeviceId(ContactID const& id) override {
    device_id_ = id;
  }

  void SetSecretKey(std::span<std::byte const> key) override {
    device_secret_key_.assign(key.begin(), key.end());
  }

  void SetBootstrapNodeUrls(std::vector<std::string> const& urls) override {
    bootstrap_urls_ = urls;
  }

  void Start() override {
    if (is_started_) return;
    is_started_ = true;

    if (!node_) {
      node_ = std::make_shared<Libp2pNode>(device_secret_key_, role_);

      node_->SetOnConnected([this](ContactID const& peer_id, bool is_circuit) -> void {
        rx_queue_.Dispatch([this, peer_id, is_circuit]() -> void {
          auto p = GetPeer(peer_id);
          if (!p) {
            p = std::make_shared<Peer>(peer_id);
            AddPeer(peer_id, p);
          }
          if (!p->data_channel_is_open) {
            {
              std::lock_guard<std::mutex> const lock(p->queue_mutex);
              p->tx_seq = 0;
              p->rx_seq = 0;
              p->out_of_order_queue.clear();
            }
            p->data_channel_is_open = true;
            p->is_connecting = false;
            p->is_circuit = is_circuit;
            LGI << "Peer connected: " << peer_id.ToString() << (is_circuit ? " (circuit)" : " (direct)");
            EventQueue::Instance().Push(EventDataChannelOpen{peer_id});
          } else {
            LGD << "Secondary connection established with " << peer_id.ToString() << ", ignoring seq reset.";
          }
        });
      });

      node_->SetOnDisconnected([this](ContactID const& network_id) -> void {
        rx_queue_.Dispatch([this, network_id]() -> void {
          ContactID master_id = network_id;
          bool no_more_devices = true;
          {
            std::lock_guard<std::mutex> const lock(peersMutex_);
            if (auto it = peers_.find(network_id); it != peers_.end()) {
              it->second->data_channel_is_open = false;
              it->second->is_connecting = false;
              it->second->data_channel_initialized = false;
            }
            if (auto it = device_to_master_.find(network_id); it != device_to_master_.end()) {
              master_id = it->second;
              if (auto m_it = master_to_devices_.find(master_id); m_it != master_to_devices_.end()) {
                std::erase(m_it->second, network_id);
                no_more_devices = m_it->second.empty();
              }
              device_to_master_.erase(it);
            }
            peers_.erase(network_id);
          }
          if (no_more_devices) {
            EventQueue::Instance().Push(
                EventDataPeerConnectionStateChanged{master_id, PeerConnectionState::disconnected}
            );
          }
        });
      });

      node_->SetOnMessage([this](ContactID const& peer_id, std::span<std::byte const> data) -> void {
        std::vector<std::byte> payload(data.begin(), data.end());
        rx_queue_.Dispatch([this, peer_id, payload = std::move(payload)]() mutable -> void {
          if (!payload.empty() && payload[0] != std::byte{0xFE}) {
            try {
              std::string const json_str(reinterpret_cast<char const*>(payload.data()), payload.size());
              auto parsed = Json::parse(json_str);
              if (parsed.contains("s") && parsed.contains("m")) {
                uint64_t const seq = parsed["s"];
                auto inner_msg = parsed["m"];
                if (auto peer = this->GetPeer(peer_id)) {
                  std::lock_guard<std::mutex> const lock(peer->queue_mutex);
                  if (seq >= peer->rx_seq) {
                    peer->out_of_order_queue[seq] = inner_msg;
                    while (!peer->out_of_order_queue.empty()) {
                      auto it = peer->out_of_order_queue.begin();
                      if (it->first == peer->rx_seq) {
                        auto msg_to_process = it->second;
                        peer->rx_seq++;
                        peer->out_of_order_queue.erase(it);

                        std::string const inner_str = msg_to_process.dump();
                        std::vector<std::byte> inner_payload(inner_str.size());
                        std::memcpy(inner_payload.data(), inner_str.data(), inner_str.size());
                        EventQueue::Instance().Push(EventDataChannelMessage{peer_id, std::move(inner_payload)});
                      } else {
                        break;
                      }
                    }
                  } else {
                    LGE << "Received stale sequence number: " << seq << " (expected >= " << peer->rx_seq << ")";
                  }
                  return;
                }
              }
            } catch (...) {
              // Fallback to direct dispatch for unsequenced/legacy messages
            }
          }
          EventQueue::Instance().Push(EventDataChannelMessage{peer_id, std::move(payload)});
        });
      });

      node_->SetOnTurnRelaysDiscovered([this](std::vector<ContactID> const& relays) -> void {
        rx_queue_.Dispatch([relays]() -> void { EventQueue::Instance().Push(EventTurnRelaysDiscovered{relays}); });
      });

      node_->SetOnTurnAuthResult(
          [this](
              ContactID const& peer_id,
              bool success,
              std::string const& ip,
              uint16_t port,
              std::string const& user,
              std::string const& cred
          ) -> void {
            rx_queue_.Dispatch([peer_id, success, ip, port, user, cred]() -> void {
              EventQueue::Instance().Push(EventTurnAuthResult{peer_id, success, ip, port, user, cred});
            });
          }
      );

      node_->SetOnNetworkStats(
          [this](int connected_peers, int bootstrap_nodes, int dht_peers, std::string const& role) -> void {
            rx_queue_.Dispatch([connected_peers, bootstrap_nodes, dht_peers, role]() -> void {
              EventQueue::Instance().Push(EventNetworkStatsUpdated{connected_peers, bootstrap_nodes, dht_peers, role});
            });
          }
      );

      node_->SetOnProvidersDiscovered(
          [this](std::string const& key_hex, std::vector<ContactID> const& providers) -> void {
            rx_queue_.Dispatch([this, key_hex, providers]() -> void {
              for (auto const& provider_id : providers) {
                LGI << "Discovered provider " << provider_id.ToString() << " for DHT key " << key_hex;
                node_->Dial(provider_id);
              }
            });
          }
      );
    }

    node_->Start();
    node_->StartProviding(master_id_.ToString());

    if (!bootstrap_urls_.empty()) {
      for (auto const& url : bootstrap_urls_) {
        LGI << "Connecting to bootstrap: " << url;
        node_->AddBootstrapNode(url);
      }
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
      master_to_devices_.clear();
      device_to_master_.clear();
    }
    for (auto const& id : to_notify) {
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{id, PeerConnectionState::disconnected});
    }

    if (node_) {
      node_->Reconnect();
      for (auto const& url : bootstrap_urls_) {
        LGI << "Re-adding bootstrap node: " << url;
        node_->AddBootstrapNode(url);
      }
    }
    is_reconnecting_ = false;
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_[peer_id] = peer;
  }

  void RemovePeer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    if (auto it = master_to_devices_.find(peer_id); it != master_to_devices_.end()) {
      for (auto const& net_id : it->second) {
        peers_.erase(net_id);
        device_to_master_.erase(net_id);
      }
      master_to_devices_.erase(it);
    }
    peers_.erase(peer_id);
  }

  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    if (auto it = master_to_devices_.find(peer_id); it != master_to_devices_.end() && !it->second.empty()) {
      auto dev_it = peers_.find(it->second.front());
      if (dev_it != peers_.end()) return dev_it->second;
    }
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

  auto GetMasterId(ContactID const& network_id) const -> ContactID override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    if (auto it = device_to_master_.find(network_id); it != device_to_master_.end()) {
      return it->second;
    }
    return network_id;
  }

  void LinkDeviceToMaster(ContactID const& network_id, ContactID const& master_id) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    if (network_id != master_id) {
      auto& vec = master_to_devices_[master_id];
      if (std::find(vec.begin(), vec.end(), network_id) == vec.end()) {
        vec.push_back(network_id);
      }
      device_to_master_[network_id] = master_id;
      peers_.erase(master_id);
    }
    EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{master_id, PeerConnectionState::connected});
  }

  void Connect(ContactID const& peer_id) override {
    if (node_) {
      auto p = GetPeer(peer_id);
      if (!p) {
        p = std::make_shared<Peer>(peer_id);
        AddPeer(peer_id, p);
      }
      p->is_connecting = true;
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::connecting});

      node_->GetProviders(peer_id.ToString());

      rx_queue_.DispatchAfter(std::chrono::seconds(15), [this, peer_id]() {
        std::lock_guard<std::mutex> const lock(peersMutex_);
        if (auto it = peers_.find(peer_id); it != peers_.end()) {
          if (it->second->is_connecting && !it->second->data_channel_is_open) {
            it->second->is_connecting = false;
            EventQueue::Instance().Push(
                EventDataPeerConnectionStateChanged{peer_id, PeerConnectionState::disconnected}
            );
          }
        }
      });
    }
  }

  void Disconnect(ContactID const& peer_id) override {
    std::vector<ContactID> networks;
    bool changed = false;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      if (auto it = master_to_devices_.find(peer_id); it != master_to_devices_.end()) {
        networks = it->second;
        master_to_devices_.erase(it);
      } else {
        networks.push_back(peer_id);
      }
      for (auto const& net_id : networks) {
        if (auto it = peers_.find(net_id); it != peers_.end()) {
          auto& p = it->second;
          if (p->data_channel_is_open || p->is_connecting) {
            p->data_channel_is_open = false;
            p->is_connecting = false;
            p->data_channel_initialized = false;
            changed = true;
          }
        }
        device_to_master_.erase(net_id);
      }
    }
    if (node_) {
      for (auto const& net_id : networks) {
        node_->Disconnect(net_id);
      }
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
    std::vector<ContactID> networks;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      if (auto it = master_to_devices_.find(peer_id); it != master_to_devices_.end() && !it->second.empty()) {
        networks = it->second;
      } else {
        networks.push_back(peer_id);
      }
    }
    bool any = false;
    for (auto const& net : networks) {
      if (node_ && node_->SendFile(net, message)) any = true;
    }
    return any;
  }

  auto SendJson(ContactID const& peer_id, Json const& message) -> bool override {
    std::vector<ContactID> networks;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      if (auto it = master_to_devices_.find(peer_id); it != master_to_devices_.end() && !it->second.empty()) {
        networks = it->second;
      } else {
        networks.push_back(peer_id);
      }
    }

    bool any_success = false;
    for (auto const& net_id : networks) {
      uint64_t seq = 0;
      {
        std::lock_guard<std::mutex> const lock(peersMutex_);
        if (auto it = peers_.find(net_id); it != peers_.end()) {
          seq = it->second->tx_seq++;
        }
      }
      Json const wrapped = {{"s", seq}, {"m", message}};
      std::string const json_str = wrapped.dump();
      std::vector<std::byte> payload(json_str.size());
      std::memcpy(payload.data(), json_str.data(), json_str.size());

      if (node_ && node_->SendChat(net_id, payload)) any_success = true;
    }
    return any_success;
  }

  void DiscoverTurnRelays() override {
    if (node_) {
      node_->DiscoverTurnRelays();
    }
  }

  void AuthTurnRelay(ContactID const& peer_id) override {
    if (node_) {
      node_->AuthTurnRelay(peer_id);
    }
  }

  void SetTurnEligibility(bool eligible) override {
    if (node_) {
      node_->SetTurnEligibility(eligible);
    }
  }
};

}  // namespace eillance::transport
