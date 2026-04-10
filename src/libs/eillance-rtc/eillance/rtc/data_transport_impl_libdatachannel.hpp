#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/connection_state_conversion.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/protocol.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::rtc {

class RtcTrack : public transport::ITrack {
  std::shared_ptr<::rtc::Track> track_;
  std::shared_ptr<::rtc::RtcpSrReporter> reporter_;
  std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config_;

 public:
  RtcTrack(
      std::shared_ptr<::rtc::Track> track,
      std::shared_ptr<::rtc::RtcpSrReporter> reporter,
      std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config
  )
      : track_(std::move(track)),
        reporter_(std::move(reporter)),
        rtp_config_(std::move(rtp_config)) {
  }

  void SendSample(std::vector<std::byte> const& sample, uint64_t sample_time) override {
    auto const elapsed_seconds = static_cast<double>(sample_time) / Time::uS;
    auto const elapsed_timestamp = rtp_config_->secondsToTimestamp(elapsed_seconds);
    rtp_config_->timestamp = rtp_config_->startTimestamp + elapsed_timestamp;
    if (!sample.empty()) {
      track_->send(sample);
    }
  }

  [[nodiscard]] auto GetTrack() const {
    return track_;
  }
};

class RtcDataTransport : public transport::DataTransport::Interface {
 public:
  using Json = nlohmann::json;
  using Peer = transport::Peer;

  RtcDataTransport() {
    // Default fallback config
    rtcConfig_.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
  }

  RtcDataTransport(RtcDataTransport&&) = delete;
  RtcDataTransport(RtcDataTransport const&) = delete;
  auto operator=(RtcDataTransport&&) -> RtcDataTransport& = delete;
  auto operator=(RtcDataTransport const&) -> RtcDataTransport& = delete;
  ~RtcDataTransport() override = default;

  auto GetServiceConnectionState() const -> ServiceConnectionState {
    return serviceConnectionState_;
  }

  void SetName(std::string const& device_name) {
    deviceName_ = device_name;
  }

  void SetId(ContactID const& id) override {
    INVARIANT(!is_logged_in_);
    id_ = id;
    if (deviceName_.empty()) {
      deviceName_ = id_.ToString();
    }
  }

  void SetSignalingUrl(std::string const& url) override {
    signalingServerUrl_ = url;
  }

  void Start() override {
    INVARIANT(!id_.IsEmpty());
    LGI << "The local ID is: " << id_;
    terminate_requested_ = false;
    ConnectWebSocket();
  }

  void Terminate() override {
    terminate_requested_ = true;
    if (ws_ != nullptr) {
      if (ws_->isOpen()) {
        ws_->close();
      }
      ws_->resetCallbacks();
    }
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
  }

  void Reconnect() override {
    if (ws_ != nullptr) {
      LGI << "Reconnect requested. Closing current WebSocket connection to re-establish.";
      if (ws_->isOpen()) {
        ws_->close();
      }
    }
  }

  void SetIceServers(std::string const& json_config) override {
    try {
      auto json = Json::parse(json_config);
      if (!json.is_array()) {
        LGE << "ICE Configuration must be a JSON array. Got: " << json_config;
        return;
      }

      ::rtc::Configuration new_config;
      for (auto const& entry : json) {
        if (!entry.contains("urls")) {
          continue;
        }

        auto urls_entry = entry["urls"];
        std::string url;

        if (urls_entry.is_array() && !urls_entry.empty()) {
          url = urls_entry[0].get<std::string>();
        } else if (urls_entry.is_string()) {
          url = urls_entry.get<std::string>();
        }

        if (url.empty()) {
          continue;
        }

        if (url.rfind("turn", 0) == 0) {
          std::string const username = entry.value("username", "");
          std::string const credential = entry.value("credential", "");
          if (!username.empty() && !credential.empty()) {
            auto scheme_end = url.find(":");
            if (scheme_end != std::string::npos) {
              std::string const host_part = url.substr(scheme_end + 1);
              new_config.iceServers.emplace_back("turn:" + username + ":" + credential + "@" + host_part);
            } else {
              new_config.iceServers.emplace_back(url);
            }
          } else {
            new_config.iceServers.emplace_back(url);
          }
        } else {
          new_config.iceServers.emplace_back(url);
        }
      }

      if (!new_config.iceServers.empty()) {
        std::lock_guard<std::mutex> const lock(configMutex_);
        rtcConfig_ = new_config;
        LGI << "Updated ICE Configuration with " << rtcConfig_.iceServers.size() << " servers.";
      }
    } catch (std::exception const& e) {
      LGE << "Failed to parse ICE configuration: " << e.what();
    }
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.emplace(peer_id, peer);
  }

  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    auto const it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }

  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    return peers_;
  }

  auto GetConnectionState(ContactID const& peer_id) const -> transport::PeerConnectionState override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    if (auto const it = peers_.find(peer_id); it != peers_.end() && it->second && it->second->data_peer_connection) {
      return transport::ToEillanceState(
          std::static_pointer_cast<::rtc::PeerConnection>(it->second->data_peer_connection)->state()
      );
    }
    return transport::PeerConnectionState::disconnected;
  }

  void RemovePeer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.erase(peer_id);
  }

  void Connect(ContactID const& peer_id) override {
    CreatePeerConnection(peer_id);
  }

  void Disconnect(ContactID const& peer_id) override {
    if (auto peer = GetPeer(peer_id)) {
      peer->data_channel_is_open = false;
      if (peer->data_peer_connection) {
        std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection)->close();
      }
    }
  }

  void CreateOffer(ContactID const& peer_id) override {
    auto peer = GetPeer(peer_id);
    if (!peer || !peer->data_peer_connection) {
      return;
    }

    CreateDataChannel(peer_id);

    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection);
    pc->setLocalDescription();
  }

  void CreateAnswer(ContactID const& peer_id) override {
    TryCreateAnswer(peer_id, 0);
  }

  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) override {
    auto peer = GetPeer(peer_id);
    if (!peer || !peer->data_peer_connection) {
      return;
    }
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection);
    try {
      pc->setRemoteDescription(::rtc::Description(sdp, type));
      LGI << "Applied remote description of type '" << type << "' for " << peer_id.ToString();
    } catch (std::exception const& e) {
      LGE << "SetRemoteDescription failed for " << peer_id.ToString() << ": " << e.what();
    }
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) -> bool override {
    std::shared_ptr<::rtc::DataChannel> dc;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      auto const it = peers_.find(peer_id);
      if (it == peers_.end() || !it->second->data_channel) {
        return false;
      }
      dc = std::static_pointer_cast<::rtc::DataChannel>(it->second->data_channel);
    }

    if (dc && dc->isOpen()) {
      return dc->send(message.data(), message.size());
    }
    return false;
  }

  auto SendJson(ContactID const& peer_id, Json const& message) -> bool override {
    std::shared_ptr<::rtc::DataChannel> dc;
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      auto it = peers_.find(peer_id);
      if (it == peers_.end() || !it->second->data_channel) {
        LGW << "SendJson failed: Peer not found or no DC for " << peer_id;
        return false;
      }
      dc = std::static_pointer_cast<::rtc::DataChannel>(it->second->data_channel);
    }

    if (dc && dc->isOpen()) {
      auto const msg_str = message.dump();
      // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
      bool const result = dc->send(reinterpret_cast<std::byte const*>(msg_str.data()), msg_str.size());
      if (!result) {
        LGE << "SendJson: dc->send() returned false for " << peer_id;
      }
      return result;
    }
    LGE << "SendJson failed: DC closed for " << peer_id;
    return false;
  }

  void SendSignalingMessage(Json const& message) const override {
    WsSendMessage(message);
  }

 private:
  void ConnectWebSocket() {
    if ((ws_ != nullptr && ws_->isOpen()) || terminate_requested_) {
      return;
    }

    auto ws_config = ::rtc::WebSocketConfiguration{.disableTlsVerification = true};
    ReportServiceConnectionState(ServiceConnectionState::establishing);
    ws_ = std::make_unique<::rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() -> void { OnWebSocketOpen(); });
    ws_->onClosed([this]() -> void { OnWebSocketClosed(); });
    ws_->onError([this](std::string const& error) -> void { OnWebSocketError(error); });
    ws_->onMessage(nullptr, [this](std::string const& data) -> void { OnWebSocketMessage(data); });

    if (signalingServerUrl_.empty()) {
#if SERVER_DEBUG
      signalingServerUrl_ = "ws://127.0.0.1:8000";
#else
      signalingServerUrl_ = "wss://app.hela.me:8000";
#endif
    }
    LGI << "Connecting to signaling server: " << signalingServerUrl_;
    ws_->open(signalingServerUrl_);
  }

  void OnWebSocketOpen() {
    LGI << "WebSocket connected, signaling ready";
    connectionAttempt_ = 0;
    waitBeforeReconnect_ = minWaitBeforeReconnect;
    Login();
    std::lock_guard<std::mutex> const lock(messageQueueMutex_);
    if (!message_queue_.empty()) {
      for (auto const& msg : message_queue_) {
        ws_->send(msg);
      }
      message_queue_.clear();
    }
  }

  void OnWebSocketClosed() {
    LGW << "WebSocket connection closed";
    is_logged_in_ = false;
    {
      std::lock_guard<std::mutex> const lock(messageQueueMutex_);
      message_queue_.clear();
    }
    auto peers_to_close = GetPeers();
    for (auto const& peer : peers_to_close | std::views::values) {
      if (peer && peer->data_peer_connection) {
        std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection)->close();
      }
    }
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
    ScheduleReconnect();
  }

  void OnWebSocketError(std::string const& error) {
    LGE << "WebSocket connection failed: " << error.c_str();
    is_logged_in_ = false;
    ReportServiceConnectionState(ServiceConnectionState::failure);
  }

  void ScheduleReconnect() {
    if (terminate_requested_) {
      return;
    }
    connectionAttempt_++;
    DispatchQueue::MainThread().DispatchAfter(std::chrono::seconds(waitBeforeReconnect_), [this]() -> void {
      ConnectWebSocket();
    });
    if (connectionAttempt_ < maxConnectionAttempts) {
      waitBeforeReconnect_ = std::min(waitBeforeReconnect_ * 2, maxWaitBeforeReconnect);
    }
  }

  void OnWebSocketMessage(std::string const& data) {
    auto const message = Json::parse(data);
    if (auto const itr = message.find("from"); itr == message.end()) {
      DispatchQueue::MainThread().Dispatch([this, message]() -> void { OnSystemMessage(message); });
    } else {
      DispatchQueue::MainThread().Dispatch([message]() -> void {
        EventQueue::Instance().Push(EventPeerMessage{message});
      });
    }
  }

  void ReportServiceConnectionState(ServiceConnectionState state) const {
    serviceConnectionState_ = state;
    EventQueue::Instance().Push(EventServiceConnectionStateChanged{state});
  }

  void WsSendMessage(Json const& message) const {
    if (!ws_ || !ws_->isOpen()) {
      std::lock_guard<std::mutex> const lock(messageQueueMutex_);
      message_queue_.push_back(message.dump());
      return;
    }
    try {
      ws_->send(message.dump());
    } catch (std::exception const& e) {
      LGE << "Can't send a WebSocket message due to the exception: " << e.what();
    }
  }

  void Login() {
    WsSendMessage(transport::protocol::CreateLogin(id_));
  }

  void OnSystemMessage(Json const& message) {
    auto const itr = message.find("type");
    if (itr == message.end()) {
      return;
    }

    if (auto const type = itr->get<std::string>(); type == "error") {
      auto const error = message.value("error", "unknown");
      LGE << "Signaling error received: " << error;
      if (auto const for_peer_it = message.find("for_peer"); for_peer_it != message.end()) {
        EventQueue::Instance().Push(EventSignalingError{for_peer_it->get<ContactID>(), error});
      } else {
        if (error == "auth_failed") {
          Terminate();
        }
        ReportServiceConnectionState(ServiceConnectionState::failure);
      }
    } else {
      EventQueue::Instance().Push(EventSystemMessage{type, message});
    }
  }

  void CreatePeerConnection(ContactID const& peer_id) {
    ::rtc::Configuration config;
    {
      std::lock_guard<std::mutex> const lock(configMutex_);
      config = rtcConfig_;
    }
    auto pc = std::make_shared<::rtc::PeerConnection>(config);

    pc->onStateChange([this, peer_id](::rtc::PeerConnection::State state) -> void {
      EventQueue::Instance().Push(EventDataPeerConnectionStateChanged{peer_id, transport::ToEillanceState(state)});
    });

    pc->onGatheringStateChange([this, peer_id, pc](::rtc::PeerConnection::GatheringState state) {
      if (state == ::rtc::PeerConnection::GatheringState::Complete) {
        if (pc && pc->localDescription()) {
          auto const message = transport::protocol::CreateSdp(
              pc->localDescription()->typeString(),
              std::string(*pc->localDescription()),
              transport::protocol::values::contextData,
              peer_id
          );
          WsSendMessage(message);
        }
      }
    });

    pc->onDataChannel([this, peer_id](std::shared_ptr<::rtc::DataChannel> const& dc) -> void {
      LGI << "Incoming DataChannel from " << peer_id;
      ConfigureDataChannel(peer_id, dc);
    });

    auto peer = GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      AddPeer(peer_id, peer);
    }
    peer->data_peer_connection = pc;
    // Reset data channel to ensure we don't use a stale one from a previous connection
    peer->data_channel = nullptr;
  }

  void CreateDataChannel(ContactID const& peer_id) {
    auto peer = GetPeer(peer_id);
    if (!peer || !peer->data_peer_connection) {
      return;
    }
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection);

    auto dc = pc->createDataChannel("data");
    LGI << "Created DataChannel for " << peer_id;
    ConfigureDataChannel(peer_id, dc);
  }

  void ConfigureDataChannel(ContactID const& peer_id, std::shared_ptr<::rtc::DataChannel> const& dc) const {
    auto peer = GetPeer(peer_id);
    if (!peer) {
      return;
    }

    dc->onOpen([this, peer_id]() -> void {
      DispatchQueue::MainThread().Dispatch([this, peer_id]() -> void {
        EventQueue::Instance().Push(EventDataChannelOpen{peer_id});
      });
    });

    dc->onMessage([peer, peer_id](::rtc::message_variant message) -> void {
      DispatchQueue::MainThread().Dispatch([peer, peer_id, msg = std::move(message)]() -> void {
        if (!peer) {
          LGW << "DataChannel message received but peer not found for " << peer_id;
          return;
        }

        std::vector<std::byte> payload;
        if (std::holds_alternative<::rtc::binary>(msg)) {
          auto const& bin = std::get<::rtc::binary>(msg);
          payload.assign(bin.begin(), bin.end());
        } else if (std::holds_alternative<std::string>(msg)) {
          auto const& str = std::get<std::string>(msg);
          auto const* data = reinterpret_cast<std::byte const*>(str.data());  // NOLINT(*-pro-type-reinterpret-cast)
          payload.assign(data, std::next(data, str.size()));
        }

        if (!payload.empty()) {
          EventQueue::Instance().Push(EventDataChannelMessage{peer_id, std::move(payload)});
        }
      });
    });

    peer->data_channel = dc;

    // Handle race condition where channel opens before callbacks are attached
    if (dc->isOpen()) {
      DispatchQueue::MainThread().Dispatch([this, peer_id]() -> void {
        EventQueue::Instance().Push(EventDataChannelOpen{peer_id});
      });
    }
  }

  void TryCreateAnswer(ContactID peer_id, int attempt) {
    auto peer = GetPeer(peer_id);
    if (!peer || !peer->data_peer_connection) return;

    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection);
    if (pc->signalingState() == ::rtc::PeerConnection::SignalingState::HaveRemoteOffer) {
      try {
        pc->setLocalDescription();
        LGI << "Successfully created answer for " << peer_id.ToString();
      } catch (std::exception const& e) {
        LGE << "CreateAnswer failed for " << peer_id.ToString() << ": " << e.what();
      }
    } else if (attempt < 100) {
      DispatchQueue::MainThread().DispatchAfter(std::chrono::milliseconds(20), [this, peer_id, attempt]() {
        TryCreateAnswer(peer_id, attempt + 1);
      });
    } else {
      LGE << "CreateAnswer timed out waiting for HaveRemoteOffer state for " << peer_id.ToString();
    }
  }

  ContactID id_{};
  std::string deviceName_{};
  std::atomic<bool> is_logged_in_{false};
  std::atomic<bool> terminate_requested_{false};

  static constexpr auto minWaitBeforeReconnect = 1U;
  static constexpr auto maxWaitBeforeReconnect = 10U;
  static constexpr auto maxConnectionAttempts = 10U;

  std::unique_ptr<::rtc::WebSocket> ws_{nullptr};
  std::uint32_t connectionAttempt_{0U};
  std::uint32_t waitBeforeReconnect_{minWaitBeforeReconnect};

  mutable std::vector<std::string> message_queue_{};
  mutable std::mutex messageQueueMutex_;

  mutable ServiceConnectionState serviceConnectionState_{ServiceConnectionState::unknown};

  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_{};
  mutable std::mutex peersMutex_;

  std::string signalingServerUrl_;

  ::rtc::Configuration rtcConfig_;
  std::mutex configMutex_;
};

}  // namespace eillance::rtc
