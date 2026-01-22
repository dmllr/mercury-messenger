#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "eillance/transport/transport.hpp"

namespace eillance::transport {

// A manual mock implementation of the Transport implementation concept.
// This class satisfies the template requirements for Transport::TransportModel<T>.
class MockTransport {
  using Json = nlohmann::json;

 public:
  struct CallLog {
    std::vector<std::string> starts;
    std::vector<std::string> terminates;
    std::vector<std::string> signaling_urls;
    std::vector<std::pair<ContactID, std::string>> closed_connections;
    std::vector<std::pair<ContactID, std::string>> offers;
    std::vector<std::pair<ContactID, std::string>> answers;
    std::vector<Json> sent_signaling_messages;
    std::vector<std::pair<ContactID, Json>> sent_peer_messages;
  };

  mutable CallLog log;
  mutable std::mutex mutex_;  // Protects log and peers

  TransportDelegate* delegate = nullptr;
  Transport::StatusCallbackFunction status_callback;
  std::map<ContactID, std::shared_ptr<Peer>> peers;

  void SetDelegate(TransportDelegate* d) { delegate = d; }
  void SetStatusCallback(Transport::StatusCallbackFunction const& cb) { status_callback = cb; }
  void SetName(std::string const& /*device_name*/) {}
  void SetId(ContactID const& /*id*/) {}

  void SetSignalingUrl(std::string const& url) {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.signaling_urls.push_back(url);
  }

  void Start() {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.starts.emplace_back("Start");
  }
  void Terminate() {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.terminates.emplace_back("Terminate");
  }
  void Reconnect() {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.starts.emplace_back("Reconnect");
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) {
    std::lock_guard<std::mutex> const lock(mutex_);
    peers[peer_id] = peer;
  }

  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (auto it = peers.find(peer_id); it != peers.end()) {
      return it->second;
    }
    return nullptr;
  }

  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> {
    std::lock_guard<std::mutex> const lock(mutex_);
    std::unordered_map<ContactID, std::shared_ptr<Peer>> result;
    for (auto const& [k, v] : peers) {
      result[k] = v;
    }
    return result;
  }

  void RemovePeer(ContactID const& peer_id) {
    std::lock_guard<std::mutex> const lock(mutex_);
    peers.erase(peer_id);
  }

  auto GetPeerConnectionState(ContactID const& /*peer_id*/) const -> PeerConnectionState {
    return PeerConnectionState::connected;
  }

  void CreateDataPeerConnection(ContactID const& /*peer_id*/) {}
  void CreateMediaPeerConnection(ContactID const& /*peer_id*/) {}
  void CreateDataChannel(ContactID const& /*peer_id*/) {}

  void ClosePeerConnection(ContactID const& peer_id, std::string const& context) {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.closed_connections.emplace_back(peer_id, context);
  }

  void CreateOffer(ContactID const& peer_id, std::string const& context) {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.offers.emplace_back(peer_id, context);
  }

  void CreateAnswer(ContactID const& peer_id, std::string const& context) {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.answers.emplace_back(peer_id, context);
  }

  void SetRemoteDescription(
      ContactID const& /*peer_id*/,
      std::string const& /*context*/,
      std::string const& /*sdp*/,
      std::string const& /*type*/
  ) {}

  void AddRemoteCandidate(
      ContactID const& /*peer_id*/,
      std::string const& /*context*/,
      std::string const& /*candidate*/,
      std::string const& /*mid*/
  ) {}

  void SendSignalingMessage(Json const& message) const {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.sent_signaling_messages.push_back(message);
  }

  void SendPeerDataChannelMessage(ContactID const& peer_id, Json const& message) const {
    std::lock_guard<std::mutex> const lock(mutex_);
    log.sent_peer_messages.emplace_back(peer_id, message);
  }

  void AddToStream(std::shared_ptr<Peer> const& /*peer*/, bool /*is_adding_video*/) const {}

  auto AddVideo(ContactID const& /*peer_id*/, std::function<void(void)> const& /*on_open*/) -> std::shared_ptr<ITrack> {
    return nullptr;
  }

  auto AddAudio(ContactID const& /*peer_id*/, std::function<void(void)> const& /*on_open*/) -> std::shared_ptr<ITrack> {
    return nullptr;
  }

  // Helper for tests to simulate incoming messages
  void SimulatePeerMessage(ContactID const& peer_id, Json const& message) {
    if (delegate) delegate->ProcessPeerMessage(message);
  }
};

}  // namespace eillance::transport
