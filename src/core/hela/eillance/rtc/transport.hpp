#pragma once

#include <atomic>
#include <cstdint>
#include <cassert>
#include <memory>
#include <thread>
#include <unordered_map>
#include <functional>

#include <nlohmann/json.hpp>
#include <rtc/peerconnection.hpp>
#include <utility>
#include <rtc/rtc.h>

#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/events/event_source.h"
#include "hela/eillance/events/service_connection_state.h"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/peer.hpp"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/memory.h"
#include "hela/eillance/utils/string.hpp"

namespace hela::eillance {

/**
 * @brief Interface for RtcTransport delegate
 *
 * Defines the callback interface that RtcTransport will use to communicate with its owner
 */
class RtcTransportDelegate {
public:
  using Json = nlohmann::json;

  virtual ~RtcTransportDelegate() = default;

  /**
   * @brief Get the publish message for this transport
   * @return Json object containing the publish message
   */
  virtual Json GetPublishMessage() = 0;

  /**
   * @brief Called when a peer is ready
   * @param peer The peer that is ready
   */
  virtual void OnPeerReady(const std::shared_ptr<Peer>& peer) = 0;

  /**
   * @brief Process a message from a peer
   * @param message The message to process
   */
  virtual void ProcessPeerMessage(Json message) = 0;

  /**
   * @brief Process a system message
   * @param type The message type
   * @param message The message to process
   */
  virtual void ProcessSystemMessage(const std::string& type, Json message) = 0;

  /**
   * @brief Add a video track to a peer connection
   * @param pc The peer connection
   * @param onOpen Callback to be called when the track is opened
   * @return The created track
   */
  virtual std::shared_ptr<Peer::Track> AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) = 0;

  /**
   * @brief Add an audio track to a peer connection
   * @param pc The peer connection
   * @param onOpen Callback to be called when the track is opened
   * @return The created track
   */
  virtual std::shared_ptr<Peer::Track> AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) = 0;
};

class RtcTransport {
  using Json = nlohmann::json;
  using StatusCallbackFunction = std::function<void(const std::string& peer_id, EventSource, int)>;

  std::string id_{};
  std::string device_name_{id_};
  std::string auth_token_{};
  std::atomic<bool> is_published_{false};
  std::atomic<bool> is_offline_{false};

  static constexpr auto minWaitBeforeReconnect = 1U;
  static constexpr auto maxWaitBeforeReconnect = 10U;
  static constexpr auto maxConnectionAttempts = 10U;

  std::unique_ptr<rtc::WebSocket> ws_{nullptr};
  std::uint32_t connection_attempt_{0U};
  std::uint32_t wait_before_reconnect_{minWaitBeforeReconnect};

  mutable ServiceConnectionState service_connection_state_{ServiceConnectionState::unknown};
  std::shared_ptr<StatusCallbackFunction> status_callback_{};

  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;

  // Delegate for callbacks
  RtcTransportDelegate* delegate_{nullptr};

public:
  /**
   * @brief Construct an RtcTransport.
   * @param delegate A non-null pointer to an object implementing the IRtcTransportDelegate interface.
   *                 The delegate's lifetime must exceed the RtcTransport's lifetime.
   */
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

  [[maybe_unused]] auto GetServiceConnectionState() {
    return service_connection_state_;
  }

  void SetName(const std::string& device_name) {
    device_name_ = device_name;
    if (is_published_) {
      DispatchQueue::MainThread().Dispatch([this]() {
        Publish();
      });
    }
  }

  void SetId(const std::string& id) {
    assert(!is_published_);
    id_ = id;
    if (device_name_.empty()) {
      device_name_ = id_;
    }
  }

  void UpdateAuthToken(const std::string& token) {
    auth_token_ = token;
    DispatchQueue::MainThread().Dispatch([this]() {
      Publish();
    });
  }

  void Start() {
    assert(!id_.empty());
    LOGI << "The local ID is: " << id_.data();

    if (ws_ != nullptr && ws_->isOpen()) {
      LOGW << "The streamer service for ID " << id_.data() << " is started already, skipping...";
      return;
    }

    auto ws_config = rtc::WebSocketConfiguration{
        .disableTlsVerification = true,
        .pingInterval = std::chrono::milliseconds(1000),
    };

    ReportServiceConnectionState(ServiceConnectionState::establishing);

    ws_ = std::make_unique<rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() {
      OnWebSocketOpen(); });
    ws_->onClosed([this]() {
      OnWebSocketClosed(); });
    ws_->onError([this](const std::string& error) {
      OnWebSocketError(error); });
    ws_->onMessage(nullptr, [this](const std::string& data) {
      OnWebSocketMessage(data); });

    LOGI << "Signaling server URL is " << signalingServerUrl;

    try {
      ws_->open(signalingServerUrl);

      LOGI << "Waiting for signaling to be connected...";
      while (!ws_->isOpen()) {
        if (ws_->isClosed()) {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      connection_attempt_ = 0;
      is_offline_ = false;
    } catch (const std::exception& e) {
      LOGI << "Error: " << e.what();
      if (ws_->isOpen()) {
        ws_->close();
      }
      ReportServiceConnectionState(ServiceConnectionState::failure);
    }
  }

  void StopStreaming() {
    is_offline_ = true;
    for (auto& [peer_id, peer] : peers_) {
      auto state = static_cast<int>(peer->GetState());
      LOGI << "Closing peer " << peer_id << " in a " << state << " state.";
      if ((peer->data_channel_ != nullptr) && peer->data_channel_->isOpen()) {
        SendTerminationMessage(peer);
        peer->data_channel_->close();
      }
      peer->peer_connection_->close();
    }
  }

  void Terminate() {
    if (ws_ != nullptr) {
      StopStreaming();
      ws_->resetCallbacks();
      if (ws_->isOpen()) {
        ws_->close();
      }
    }
  }

  void AddPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) {
    peers_.emplace(std::string(peer_id), peer);
  }

  auto GetPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> {
    if (auto it = peers_.find(peer_id); it != peers_.end()) {
      return peers_.at(peer_id);
    }
    return nullptr;
  }

  auto GetPeers() const {
    return peers_;
  }

  void RemovePeer(const std::string& peer_id) {
    peers_.erase(peer_id);
  }

  auto CreatePeer(const std::string& peer_id) -> std::shared_ptr<Peer> {
    auto peer = std::make_shared<Peer>(CreatePeerConnection(peer_id));

    peer->video_ = delegate_->AddVideo(
        peer->peer_connection_,
        [this, peer_id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().Dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, true);
            }
          });
          LOGI << "Video from " << id_ << " opened";
        });

    peer->audio_ = delegate_->AddAudio(
        peer->peer_connection_,
        [this, peer_id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().Dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, false);
            }
          });
          LOGI << "Audio from " << id_ << " opened";
        });

    return peer;
  }

  void SendPeerMessage(const std::string& peer_id, Json& message) {
    message["token"] = auth_token_;
    message["to"] = peer_id;
    WsSendMessage(message);
  }

  void SendPeerDataChannelMessage(const std::string& peer_id, const Json& message) {
    auto peer = GetPeer(peer_id);
    if (peer && peer->data_channel_) {
      peer->data_channel_->send(message.dump());
    }
  }

  void ReportPeerStateChange(const std::string& peer_id, const EventSource source, int state) const {
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state);
    }
  }

private:
  void OnWebSocketOpen() {
    LOGI << "WebSocket connected, signaling ready";
    connection_attempt_ = 0;
    DispatchQueue::MainThread().Dispatch([this]() {
      if (!auth_token_.empty()) {
        Publish();
      }
    });

    ReportServiceConnectionState(ServiceConnectionState::online);
  }

  void OnWebSocketClosed() {
    LOGE << "WebSocket connection closed";
    ReportServiceConnectionState(ServiceConnectionState::failure);

    if (is_offline_) {
      return;
    }

    connection_attempt_++;
    if (connection_attempt_ <= 1) {
      wait_before_reconnect_ = 0;
    } else if (connection_attempt_ > 1 && connection_attempt_ < maxConnectionAttempts) {
      wait_before_reconnect_ = minWaitBeforeReconnect;
    } else {
      wait_before_reconnect_ = maxWaitBeforeReconnect;
    }

    DispatchQueue::MainThread().Dispatch([this]() {
      std::this_thread::sleep_for(std::chrono::seconds(wait_before_reconnect_));
      Start();
    });
    LOGI << "Scheduled WebSocket reconnect in " << wait_before_reconnect_ << "s";
  }

  void OnWebSocketError(const std::string& error) {
    LOGE << "WebSocket connection failed: " << error.c_str();
    ReportServiceConnectionState(ServiceConnectionState::failure);
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

  void WsSendMessage(const Json& message) {
    if (!ws_->isOpen()) {
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

  void Publish() {
    if (auth_token_.empty()) {
      return;
    }

    auto message = GetPublishMessage();
    message["type"] = "publish";
    message["token"] = auth_token_;
    message["peer_id"] = id_;
    message["name"] = device_name_;

    WsSendMessage(message);
    is_published_ = true;
  }

  Json GetPublishMessage() {
    return delegate_->GetPublishMessage();
  }

  auto CreatePeerConnection(const std::string& peer_id) -> std::shared_ptr<rtc::PeerConnection> {
    auto peer_connection = std::make_shared<rtc::PeerConnection>(RtcConfiguration::Config());

    peer_connection->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
      LOGI << "State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::peerConnectionState, static_cast<int>(state));
      if (state == rtc::PeerConnection::State::Disconnected ||
          state == rtc::PeerConnection::State::Failed ||
          state == rtc::PeerConnection::State::Closed) {
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
        if (auto peer_con = wpc.lock()) {
          const auto description = peer_con->localDescription();
          const Json message = {
              {"to", peer_id},
              {"token", auth_token_},
              {"type", description->typeString()},
              {"sdp", std::string(description.value())}};
          WsSendMessage(message);
        }
      }
    });

    peer_connection->onSignalingStateChange([this, peer_id](rtc::PeerConnection::SignalingState state) {
      LOGI << "Signaling State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::signalingState, static_cast<int>(state));
    });

    return std::move(peer_connection);
  }

  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) {
    if (peer->GetState() == Peer::State::waiting) {
      peer->SetState(isAddingVideo ? Peer::State::waitingForAudio : Peer::State::waitingForVideo);
    } else if ((peer->GetState() == Peer::State::waitingForAudio && !isAddingVideo) || (peer->GetState() == Peer::State::waitingForVideo && isAddingVideo)) {
      // Audio and video tracks are collected now
      assert(peer->video_ != nullptr && peer->audio_ != nullptr);
      peer->SetState(Peer::State::ready);
    }

    if (peer->GetState() == Peer::State::ready && delegate_) {
      delegate_->OnPeerReady(peer);
    }
  }

  void OnSystemMessage(Json message) {
    auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();

    if (type == "error") {
      LOGE << "Signaling error received: " << message["error"].get<std::string>();
      ReportServiceConnectionState(ServiceConnectionState::failure);
    } else if (type == "peer_disconnected") {
      const auto disconnected_peer_id = message["id"].get<std::string>();
      RemovePeer(disconnected_peer_id);
    } else {
      delegate_->ProcessSystemMessage(type, message);
    }
  }

  static void SendTerminationMessage(const std::shared_ptr<Peer>& peer) {
    if (!peer->data_channel_->isOpen()) {
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

#if 1 // NOLINT(*-avoid-unconditional-preprocessor-if)
  constexpr static auto signalingServerUrl = "wss://app.hela.me:8000";
#else
#if ANDROID
  constexpr static auto signalingServerUrl = "wss://10.0.2.2:8888";
#else
  constexpr static auto signalingServerUrl = "wss://127.0.0.1:8888";
#endif
#endif
};

}  // namespace hela::eillance
