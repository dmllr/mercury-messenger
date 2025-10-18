#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <rtc/peerconnection.hpp>
#include <utility>

#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/events/event_source.h"
#include "eillance/events/service_connection_state.h"
#include "eillance/logging/logging.h"
#include "eillance/rtc/peer.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/utils/memory.h"

namespace eillance {

class RtcTransportDelegate {
public:
  using Json = nlohmann::json;

  virtual ~RtcTransportDelegate() = default;
  virtual void OnPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void OnPeerOnline(const std::string& peer_id) = 0;
  virtual void OnSignalingErrorForPeer(const std::string& peer_id, const std::string& error) = 0;
  virtual void ProcessPeerMessage(Json message) = 0;
  virtual void ProcessSystemMessage(const std::string& type, Json message) = 0;
  virtual std::shared_ptr<Peer::Track> AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) = 0;
  virtual std::shared_ptr<Peer::Track> AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) = 0;
  virtual void OnTrack(const std::string& peer_id, std::shared_ptr<rtc::Track> track) = 0;
};

class RtcTransport {
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(const std::string& peer_id, EventSource, int)>;

  std::string id_{};
  std::string device_name_{id_};
  std::atomic<bool> is_logged_in_{false};
  std::atomic<bool> terminate_requested_{false};

  static constexpr auto minWaitBeforeReconnect = 1U;
  static constexpr auto maxWaitBeforeReconnect = 10U;
  static constexpr auto maxConnectionAttempts = 10U;

  std::unique_ptr<rtc::WebSocket> ws_{nullptr};
  std::uint32_t connection_attempt_{0U};
  std::uint32_t wait_before_reconnect_{minWaitBeforeReconnect};

  mutable ServiceConnectionState service_connection_state_{ServiceConnectionState::unknown};
  std::shared_ptr<StatusCallbackFunction> status_callback_{};

  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peers_mutex_;

  RtcTransportDelegate* delegate_{nullptr};

public:
  explicit RtcTransport(RtcTransportDelegate* delegate) : delegate_(delegate) {
    assert(delegate_ != nullptr && "RtcTransport requires a valid delegate");
  }

  RtcTransport() = delete;
  RtcTransport(RtcTransport&&) = delete;
  RtcTransport(const RtcTransport&) = delete;
  auto operator=(RtcTransport&&) -> RtcTransport& = delete;
  auto operator=(const RtcTransport&) -> RtcTransport& = delete;
  ~RtcTransport() = default;

  [[maybe_unused]] void SetStatusCallback(const StatusCallbackFunction& callback) {
    status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
  }

  [[maybe_unused]] auto GetServiceConnectionState() const {
    return service_connection_state_;
  }

  void SetName(const std::string& device_name) {
    device_name_ = device_name;
  }

  void SetId(const std::string& id) {
    assert(!is_logged_in_);
    id_ = id;
    if (device_name_.empty()) {
      device_name_ = id_;
    }
  }

  void Start() {
    assert(!id_.empty());
    LOGI << "The local ID is: " << id_.data();
    terminate_requested_ = false;
    ConnectWebSocket();
  }

  void StopStreaming() {
    std::unordered_map<std::string, std::shared_ptr<Peer>> peersToDestroy;
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      peersToDestroy.swap(peers_);
    }
  }

  void Terminate() {
    terminate_requested_ = true;
    if (ws_ != nullptr) {
      StopStreaming();
      if (ws_->isOpen()) {
        ws_->close();
      }
      ws_->resetCallbacks();
    }
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
  }

  void AddPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.emplace(std::string(peer_id), peer);
  }

  auto GetPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }

  auto GetPeers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_;
  }

  void RemovePeer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
  }

  auto CreatePeer(const std::string& peer_id) -> std::shared_ptr<Peer> {
    auto peer = std::make_shared<Peer>(CreatePeerConnection(peer_id));
    return peer;
  }

  void SendSignalingMessage(const Json& message) const {
    WsSendMessage(message);
  }

  void SendPeerDataChannelMessage(const std::shared_ptr<rtc::DataChannel>& dc, const Json& message) {
    if (dc && dc->isOpen()) {
      dc->send(message.dump());
    }
  }

  void ReportPeerStateChange(const std::string& peer_id, const EventSource source, int state) const {
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state);
    }
  }

  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const {
    if (peer->GetState() == Peer::State::waiting) {
      peer->SetState(isAddingVideo ? Peer::State::waitingForAudio : Peer::State::waitingForVideo);
    } else if ((peer->GetState() == Peer::State::waitingForAudio && !isAddingVideo) ||
               (peer->GetState() == Peer::State::waitingForVideo && isAddingVideo)) {
      assert(peer->video_ != nullptr && peer->audio_ != nullptr);
      peer->SetState(Peer::State::ready);
    }

    if (peer->GetState() == Peer::State::ready && delegate_) {
      delegate_->OnPeerReady(peer);
    }
  }

private:
  void ConnectWebSocket() {
      if (ws_ != nullptr && ws_->isOpen()) {
          LOGW << "WebSocket is already open, skipping connection attempt.";
          return;
      }
      if (terminate_requested_) {
          LOGI << "Terminate requested, aborting connection attempt.";
          return;
      }

      auto ws_config = rtc::WebSocketConfiguration{
          .disableTlsVerification = true,
      };

      ReportServiceConnectionState(ServiceConnectionState::establishing);
      ws_ = std::make_unique<rtc::WebSocket>(ws_config);
      ws_->onOpen([this]() { OnWebSocketOpen(); });
      ws_->onClosed([this]() { OnWebSocketClosed(); });
      ws_->onError([this](const std::string& error) { OnWebSocketError(error); });
      ws_->onMessage(nullptr, [this](const std::string& data) { OnWebSocketMessage(data); });

      LOGI << "Connecting to signaling server: " << signalingServerUrl;
      ws_->open(signalingServerUrl);
  }


  void OnWebSocketOpen() {
    LOGI << "WebSocket connected, signaling ready";
    connection_attempt_ = 0;
    wait_before_reconnect_ = minWaitBeforeReconnect;
    Login();
  }

  void OnWebSocketClosed() {
      LOGW << "WebSocket connection closed";
      is_logged_in_ = false;

      std::vector<std::shared_ptr<Peer>> peers_to_close;
      {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for(const auto& [id, peer] : peers_) {
          peers_to_close.push_back(peer);
        }
      }

      for (const auto& peer : peers_to_close) {
          if(peer && peer->peer_connection_) {
              peer->peer_connection_->close();
          }
      }

      ReportServiceConnectionState(ServiceConnectionState::disconnected);
      ScheduleReconnect();
  }

  void OnWebSocketError(const std::string& error) {
    LOGE << "WebSocket connection failed: " << error.c_str();
    is_logged_in_ = false;
    ReportServiceConnectionState(ServiceConnectionState::failure);
    // onClosed will be called, which will schedule a reconnect
  }

  void ScheduleReconnect() {
      if (terminate_requested_) {
          return;
      }

      connection_attempt_++;
      LOGI << "Attempting to reconnect in " << wait_before_reconnect_ << "s (attempt " << connection_attempt_ << ")";
      DispatchQueue::MainThread().DispatchAfter(std::chrono::seconds(wait_before_reconnect_), [this]() {
          ConnectWebSocket();
      });

      // Exponential backoff
      if (connection_attempt_ < maxConnectionAttempts) {
          wait_before_reconnect_ = std::min(wait_before_reconnect_ * 2, maxWaitBeforeReconnect);
      }
  }


  void OnWebSocketMessage(const std::string& data) {
    const auto message = Json::parse(data);
    auto itr = message.find("from");
    if (itr == message.end()) {
      DispatchQueue::MainThread().Dispatch([this, message]() {
        OnSystemMessage(message);
      });
    } else {
      DispatchQueue::MainThread().Dispatch([this, message]() {
        delegate_->ProcessPeerMessage(message);
      });
    }
  }

  void ReportServiceConnectionState(const ServiceConnectionState state) const {
    service_connection_state_ = state;
    if (status_callback_) {
      (*status_callback_)("", EventSource::system, static_cast<int>(state));
    }
  }

  void WsSendMessage(const Json& message) const {
    if (!ws_ || !ws_->isOpen()) {
      LOGE << "Can't send a WebSocket message, the connection has been closed";
      return;
    }
    try {
      ws_->send(message.dump());
    } catch (const std::runtime_error& e) {
      LOGE << "Can't send a WebSocket message due to the exception: " << e.what();
    } catch (const std::exception& e) {
      LOGE << "Can't send a WebSocket message due to the exception: " << e.what();
    }
  }

  void Login() {
    const Json message = {
        {"type", "login"},
        {"id", id_},
    };

    WsSendMessage(message);
    is_logged_in_ = true;
    ReportServiceConnectionState(ServiceConnectionState::online);
  }

  auto CreatePeerConnection(const std::string& peer_id) -> std::shared_ptr<rtc::PeerConnection> {
    auto peer_connection = std::make_shared<rtc::PeerConnection>(RtcConfiguration::Config());

    peer_connection->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
      std::string stateStr = "unknown";
      switch(state) {
          case rtc::PeerConnection::State::New: stateStr = "New"; break;
          case rtc::PeerConnection::State::Connecting: stateStr = "Connecting"; break;
          case rtc::PeerConnection::State::Connected: stateStr = "Connected"; break;
          case rtc::PeerConnection::State::Disconnected: stateStr = "Disconnected"; break;
          case rtc::PeerConnection::State::Failed: stateStr = "Failed"; break;
          case rtc::PeerConnection::State::Closed: stateStr = "Closed"; break;
      }
      LOGI << "Peer " << peer_id << " state changed to " << stateStr << " (" << static_cast<int>(state) << ")";
      ReportPeerStateChange(peer_id, EventSource::peerConnectionState, static_cast<int>(state));

      if (state == rtc::PeerConnection::State::Failed ||
          state == rtc::PeerConnection::State::Disconnected) {
        if (const auto peer = GetPeer(peer_id)) {
            LOGI << "Closing failed/disconnected peer connection for " << peer_id;
            peer->peer_connection_->close();
        }
      } else if (state == rtc::PeerConnection::State::Closed) {
          LOGI << "Peer connection for " << peer_id << " is closed. Removing from transport.";
          DispatchQueue::MainThread().Dispatch([this, peer_id]() {
              RemovePeer(peer_id);
          });
      }
    });

    peer_connection->onIceStateChange([this, peer_id](rtc::PeerConnection::IceState state) {
      LOGI << "ICE State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::iceState, static_cast<int>(state));
    });

    peer_connection->onGatheringStateChange([this, wpc = make_weak_ptr(peer_connection), peer_id](rtc::PeerConnection::GatheringState state) {
      LOGI << "Gathering State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::gatheringState, static_cast<int>(state));
      if (state == rtc::PeerConnection::GatheringState::Complete) {
        // Gathering complete, send answer
        if (const auto peer_con = wpc.lock()) {
          const auto description = peer_con->localDescription();
          Json message = {
              {"type", description->typeString()},
              {"sdp", std::string(description.value())}};
          message["to"] = peer_id;
          WsSendMessage(message);
        }
      }
    });

    peer_connection->onSignalingStateChange([this, peer_id](rtc::PeerConnection::SignalingState state) {
      LOGI << "Signaling State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::signalingState, static_cast<int>(state));
    });

    peer_connection->onTrack([this, peer_id](const std::shared_ptr<rtc::Track>& track) {
      delegate_->OnTrack(peer_id, track);
    });

    return peer_connection;
  }

  void OnSystemMessage(Json message) const {
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();

    if (type == "error") {
        const auto error = message.value("error", "unknown");
        LOGE << "Signaling error received: " << error;
        if (const auto for_peer_it = message.find("for_peer"); for_peer_it != message.end()) {
            delegate_->OnSignalingErrorForPeer(for_peer_it->get<std::string>(), error);
        } else {
            ReportServiceConnectionState(ServiceConnectionState::failure);
        }
    } else if (type == "peer_online") {
        if (const auto id_itr = message.find("id"); id_itr != message.end()) {
            delegate_->OnPeerOnline(id_itr->get<std::string>());
        }
    }
    else {
      delegate_->ProcessSystemMessage(type, message);
    }
  }

  static void SendTerminationMessage(const std::shared_ptr<Peer>& peer) {
    if (!peer->data_channel_ || !peer->data_channel_->isOpen()) {
      return;
    }
    const Json message = {
        {"type", "termination"},
    };
    peer->data_channel_->send(message.dump());
  }

  class RtcConfiguration {
  public:
    static auto Config() -> rtc::Configuration& {
      static rtc::Configuration config = CreateRtcConfiguration();
      return config;
    }

  private:
    static auto CreateRtcConfiguration() -> rtc::Configuration {
      rtc::Configuration config;
      config.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
      config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80");
      config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
      return config;
    }
  };

#if 0  // NOLINT(*-avoid-unconditional-preprocessor-if)
  constexpr static auto signalingServerUrl = "wss://app.hela.me:8000";
#else
#if ANDROID
  constexpr static auto signalingServerUrl = "wss://10.0.2.2:8000";
#else
  constexpr static auto signalingServerUrl = "ws://127.0.0.1:8000";
#endif
#endif
};

}  // namespace eillance
