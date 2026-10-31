#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/client_events.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::transport {

class MockDataTransport : public DataTransport::Interface {
 public:
  ContactID id_;
  ContactID device_id_;
  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  std::vector<std::byte> secret_key_;
  std::vector<std::string> bootstrap_urls_;

  std::vector<std::pair<ContactID, nlohmann::json>> sent_jsons;
  std::vector<std::pair<ContactID, std::vector<std::byte>>> sent_data;
  std::unordered_map<ContactID, ContactID> device_to_master_;

  void SetId(ContactID const& id) override {
    id_ = id;
  }
  void SetDeviceId(ContactID const& id) override {
    device_id_ = id;
  }
  void SetSecretKey(std::span<std::byte const> key) override {
    secret_key_.assign(key.begin(), key.end());
  }
  void SetBootstrapNodeUrls(std::vector<std::string> const& urls) override {
    bootstrap_urls_ = urls;
  }

  void Start() override {
  }
  void Terminate() override {
  }
  void Reconnect() override {
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    peers_[peer_id] = peer;
  }
  // Test helper: simulate a peer that completed a verified key exchange.
  void MarkVerified(ContactID const& peer_id) {
    auto p = GetPeer(peer_id);
    if (!p) {
      p = std::make_shared<Peer>(peer_id);
      peers_[peer_id] = p;
    }
    p->crcp_verified = true;
  }

  void RemovePeer(ContactID const& peer_id) override {
    peers_.erase(peer_id);
  }
  [[nodiscard]] auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    auto const it = peers_.find(peer_id);
    return it != peers_.end() ? it->second : nullptr;
  }
  [[nodiscard]] auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    return peers_;
  }
  [[nodiscard]] auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState override {
    if (auto p = GetPeer(peer_id)) {
      if (p->data_channel_is_open) {
        return PeerConnectionState::connected;
      }
      if (p->is_connecting) {
        return PeerConnectionState::connecting;
      }
    }
    return PeerConnectionState::disconnected;
  }
  [[nodiscard]] auto GetMasterId(ContactID const& network_id) const -> ContactID override {
    auto const it = device_to_master_.find(network_id);
    return it != device_to_master_.end() ? it->second : network_id;
  }
  void LinkDeviceToMaster(ContactID const& network_id, ContactID const& master_id) override {
    if (network_id != master_id) {
      device_to_master_[network_id] = master_id;
    }
  }

  void Connect(ContactID const& peer_id) override {
    if (auto p = GetPeer(peer_id)) {
      p->is_connecting = true;
    }
  }
  void Disconnect(ContactID const& peer_id) override {
    bool changed = false;
    if (auto p = GetPeer(peer_id)) {
      if (p->data_channel_is_open || p->is_connecting) {
        p->data_channel_is_open = false;
        p->is_connecting = false;
        p->data_channel_initialized = false;
        changed = true;
      }
      p->data_channel_initialized = false;
    }
    if (changed) {
      eillance::EventQueue::Instance().Push(
          eillance::EventDataPeerConnectionStateChanged{peer_id, eillance::transport::PeerConnectionState::disconnected}
      );
    }
  }

  void CreateOffer(ContactID const&) override {
  }
  void CreateAnswer(ContactID const&) override {
  }
  void SetRemoteDescription(ContactID const&, std::string const&, std::string const&) override {
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> data) -> bool override {
    std::lock_guard const lock(sent_mutex_);
    sent_data.push_back({peer_id, std::vector<std::byte>(data.begin(), data.end())});
    return true;
  }
  auto SendJson(ContactID const& peer_id, nlohmann::json const& msg) -> bool override {
    std::lock_guard const lock(sent_mutex_);
    sent_jsons.push_back({peer_id, msg});
    return true;
  }

  // Thread-safe accessors for tests that inspect sends while background services are running.
  [[nodiscard]] auto SentJsonsSnapshot() const -> std::vector<std::pair<ContactID, nlohmann::json>> {
    std::lock_guard const lock(sent_mutex_);
    return sent_jsons;
  }
  [[nodiscard]] auto SentDataSnapshot() const -> std::vector<std::pair<ContactID, std::vector<std::byte>>> {
    std::lock_guard const lock(sent_mutex_);
    return sent_data;
  }
  void ClearSent() {
    std::lock_guard const lock(sent_mutex_);
    sent_jsons.clear();
    sent_data.clear();
  }

  mutable std::mutex sent_mutex_;
  void DiscoverTurnRelays() override {
  }
  void AuthTurnRelay(ContactID const&) override {
  }
  void SetTurnEligibility(bool) override {
  }
};

}  // namespace eillance::transport
