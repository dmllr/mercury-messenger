#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <rtc/peerconnection.hpp>
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

public:
  RtcTransport() = default;
  RtcTransport(RtcTransport&&) = delete;
  RtcTransport(const RtcTransport&) = delete;
  auto operator=(RtcTransport&&) -> RtcTransport& = delete;
  auto operator=(const RtcTransport&) -> RtcTransport& = delete;
  virtual ~RtcTransport() = default;

  [[maybe_unused]] void SetStatusCallback(const StatusCallbackFunction& callback) {
    status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
  }

  [[maybe_unused]] ServiceConnectionState GetServiceConnectionState() {
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

  void Start() {
    // it is necessary to renew the peer ID in case of reconnect, so the
    // signaling server would threaten it differently until a previous
    // connection considered broken.
    id_ = StringUtils::RandomString();
    LOGI << "The local ID is: " << id_.data();

    auto ws_config = rtc::WebSocketConfiguration{
    };

    if (ws_ != nullptr) {
      ws_->resetCallbacks();
      if (ws_->isOpen()) {
        ws_->close();
        while (!ws_->isClosed()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    }
    ws_ = std::make_unique<rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() { OnWebSocketOpen(); });
    ws_->onClosed([this]() { OnWebSocketClosed(); });
    ws_->onError([this](const std::string& error) { OnWebSocketError(error); });
    ws_->onMessage(
        [](const rtc::binary&) { /* no usage for binary messages */ },
        [this](const std::string& data) { OnWebSocketMessage(data); });

    LOGD << "Signaling server URL is " << signalingServerUrl;

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
      peer->data_channel_->close();
      peer->peer_connection_->close();
    }
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
  }

  void Terminate() {
    StopStreaming();
    ws_->close();
  }

protected:
  virtual Json GetPublishMessage() = 0;
  virtual void OnPeerDataChannelConnected(const std::shared_ptr<Peer>& peer) = 0;
  virtual void OnPeerReady(const std::shared_ptr<Peer>& peer) = 0;
  virtual void ProcessPeerMessage(Json message) = 0;
  virtual void ProcessSystemMessage(const std::string& type, Json message) = 0;
  virtual auto AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> = 0;
  virtual auto AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> = 0;
  virtual void Configure(Config config) = 0;

  void AddPeer(const std::string id, const std::shared_ptr<Peer>& peer) {
    peers_.emplace(id, peer);
  }

  auto GetPeer(const std::string id) const -> std::shared_ptr<Peer> {
    if (auto it = peers_.find(id); it != peers_.end()) {
      return peers_.at(id);
    }
    return nullptr;
  }

  auto GetPeers() const {
    return peers_;
  }

  void RemovePeer(const std::string& id) {
    peers_.erase(id);
  }

  auto CreatePeerConnection(const std::string& peer_id) -> std::shared_ptr<Peer> {
    auto peer_connection = CreateDefaultPeerConnection(peer_id);

    auto peer = std::make_shared<Peer>(peer_connection);

    peer->video_ = AddVideo(
        peer_connection,
        [this, peer_id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().Dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, true);
            }
          });
          LOGI << "Video from " << id_ << " opened";
        });

    peer->audio_ = AddAudio(
        peer_connection,
        [this, peer_id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().Dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, false);
            }
          });
          LOGI << "Audio from " << id_ << " opened";
        });

    peer->data_channel_ = CreateDefaultDataChannel(peer_connection, peer_id);

    peer_connection->setLocalDescription();

    return peer;
  }

  void SendPeerMessage(Json& message) {
    message["token"] = auth_token_;
    WsSendMessage(message);
  }

private:
  void OnWebSocketOpen() {
    LOGI << "WebSocket connected, signaling ready";
    connection_attempt_ = 0;

    ReportServiceConnectionState(ServiceConnectionState::establishing);
    Authenticate();
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
        ProcessPeerMessage(message);
      });
    }
  }

  void ReportPeerStateChange(const std::string& peer_id, const EventSource source, int state) const {
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state);
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

  void Authenticate() {
    const Json message = {
        {"type", "authenticate"},
        {"username", "user1"},
        {"password", "password1"}};
    WsSendMessage(message);
  }

  void Publish() {
    auto message = GetPublishMessage();
    message["type"] = "publish";
    message["token"] = auth_token_;
    message["client_id"] = id_;
    message["name"] = device_name_;

    WsSendMessage(message);
  }

  auto CreateDefaultPeerConnection(const std::string& peer_id) -> std::shared_ptr<rtc::PeerConnection> {
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

    return peer_connection;
  }

  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) {
    if (peer->GetState() == Peer::State::waiting) {
      peer->SetState(isAddingVideo ? Peer::State::waitingForAudio : Peer::State::waitingForVideo);
    } else if ((peer->GetState() == Peer::State::waitingForAudio && !isAddingVideo) || (peer->GetState() == Peer::State::waitingForVideo && isAddingVideo)) {
      // Audio and video tracks are collected now
      assert(peer->video_ != nullptr && peer->audio_ != nullptr);
      peer->SetState(Peer::State::ready);
    }

    if (peer->GetState() == Peer::State::ready) {
      OnPeerReady(peer);
    }
  }

  auto CreateDefaultDataChannel(std::shared_ptr<rtc::PeerConnection> pc, const std::string& peer_id) -> std::shared_ptr<rtc::DataChannel> {
    auto data_channel = pc->createDataChannel("command-channel");
    data_channel->onOpen([this, peer_id, wdc = make_weak_ptr(data_channel)]() {
      if (auto dc = wdc.lock()) {
        dc->send("Ping");
      }
      OnPeerDataChannelConnected(GetPeer(peer_id));
    });

    data_channel->onMessage(nullptr, [peer_id, wdc = make_weak_ptr(data_channel), this](const std::string& msg) {
      LOGD << "Message from " << id_ << " received: " << msg;
      if (msg.rfind("Ping ", 0) == 0) {  // starts_with is not supported by all compilers
        if (auto dc = wdc.lock()) {
          dc->send("Pong");
        }
        ReportPeerStateChange(peer_id, EventSource::ping, 1);
        return;
      }
      if (msg.rfind("Pong ", 0) == 0) {  // starts_with is not supported by all compilers
        if (auto dc = wdc.lock()) {
          dc->send("Ping");
        }
        ReportPeerStateChange(peer_id, EventSource::ping, 1);
        return;
      }

      const auto message = Json::parse(msg);
      const auto itr = message.find("type");
      if (itr == message.end()) {
        return;
      }
      auto type = itr->get<std::string>();
      if (type == "configuration") {
        auto config{Config()};
        auto& video_config = config.video_config;
        video_config.cameraId = std::stoi(message["camera_id"].get<std::string>());
        video_config.width = message["resolution"]["width"].get<std::uint32_t>();
        video_config.height = message["resolution"]["height"].get<std::uint32_t>();

        Configure(config);
      }
    });

    return data_channel;
  }

  void OnSystemMessage(Json message) {
    auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();

    if (type == "authenticated") {
      auth_token_ = message["token"].get<std::string>();
      DispatchQueue::MainThread().Dispatch([this]() {
        Publish();
        is_published_ = true;
      });
    } else if (type == "error") {
      LOGE << "Signaling error received: " << message["error"].get<std::string>();
    } else {
      ProcessSystemMessage(type, message);
    }
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

  constexpr static auto signalingServerUrl = "wss://app.hela.me:8000";  // prod
//  constexpr static auto signalingServerUrl = "ws://10.0.2.2:8000";  // for android emulator
//  constexpr static auto signalingServerUrl = "ws://127.0.0.1:8000";  // for general

};

}  // namespace hela::eillance
