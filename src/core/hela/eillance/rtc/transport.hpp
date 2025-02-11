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
//        .disableTlsVerification = true,  // TODO: check
        .caCertificatePemFile = zerosslCaBundle,
//        .certificatePemFile = sscert,
//        .keyPemFile = sskey
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

  void Stop() {
    is_offline_ = true;
    for (auto& [peer_id, peer] : peers_) {
      peer->data_channel_->close();
      peer->peer_connection_->close();
    }
    ws_->close();
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
  }

protected:
  virtual Json GetPublishMessage() = 0;
  virtual void OnPeerAdded(const std::string& peer_id, const std::shared_ptr<Peer>& peer) = 0;
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
    data_channel->onOpen([peer_id, wdc = make_weak_ptr(data_channel)]() {
      if (auto dc = wdc.lock()) {
        dc->send("Ping");
      }
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

  constexpr static auto sscert =
      "-----BEGIN CERTIFICATE-----\n"
      "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\n"
      "q53DhA==\n"
      "-----END CERTIFICATE-----   ";
  constexpr static auto sskey =
      "-----BEGIN PRIVATE KEY-----\n"
      "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\n"
      "63oIhSpqW90Hc+y4va84Rse7s3Pt\n"
      "-----END PRIVATE KEY-----\n";
  constexpr static auto zerosslCaBundle =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIG1TCCBL2gAwIBAgIQbFWr29AHksedBwzYEZ7WvzANBgkqhkiG9w0BAQwFADCB\n"
      "iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n"
      "cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n"
      "BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMjAw\n"
      "MTMwMDAwMDAwWhcNMzAwMTI5MjM1OTU5WjBLMQswCQYDVQQGEwJBVDEQMA4GA1UE\n"
      "ChMHWmVyb1NTTDEqMCgGA1UEAxMhWmVyb1NTTCBSU0EgRG9tYWluIFNlY3VyZSBT\n"
      "aXRlIENBMIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAhmlzfqO1Mdgj\n"
      "4W3dpBPTVBX1AuvcAyG1fl0dUnw/MeueCWzRWTheZ35LVo91kLI3DDVaZKW+TBAs\n"
      "JBjEbYmMwcWSTWYCg5334SF0+ctDAsFxsX+rTDh9kSrG/4mp6OShubLaEIUJiZo4\n"
      "t873TuSd0Wj5DWt3DtpAG8T35l/v+xrN8ub8PSSoX5Vkgw+jWf4KQtNvUFLDq8mF\n"
      "WhUnPL6jHAADXpvs4lTNYwOtx9yQtbpxwSt7QJY1+ICrmRJB6BuKRt/jfDJF9Jsc\n"
      "RQVlHIxQdKAJl7oaVnXgDkqtk2qddd3kCDXd74gv813G91z7CjsGyJ93oJIlNS3U\n"
      "gFbD6V54JMgZ3rSmotYbz98oZxX7MKbtCm1aJ/q+hTv2YK1yMxrnfcieKmOYBbFD\n"
      "hnW5O6RMA703dBK92j6XRN2EttLkQuujZgy+jXRKtaWMIlkNkWJmOiHmErQngHvt\n"
      "iNkIcjJumq1ddFX4iaTI40a6zgvIBtxFeDs2RfcaH73er7ctNUUqgQT5rFgJhMmF\n"
      "x76rQgB5OZUkodb5k2ex7P+Gu4J86bS15094UuYcV09hVeknmTh5Ex9CBKipLS2W\n"
      "2wKBakf+aVYnNCU6S0nASqt2xrZpGC1v7v6DhuepyyJtn3qSV2PoBiU5Sql+aARp\n"
      "wUibQMGm44gjyNDqDlVp+ShLQlUH9x8CAwEAAaOCAXUwggFxMB8GA1UdIwQYMBaA\n"
      "FFN5v1qqK0rPVIDh2JvAnfKyA2bLMB0GA1UdDgQWBBTI2XhootkZaNU9ct5fCj7c\n"
      "tYaGpjAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHSUE\n"
      "FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwIgYDVR0gBBswGTANBgsrBgEEAbIxAQIC\n"
      "TjAIBgZngQwBAgEwUAYDVR0fBEkwRzBFoEOgQYY/aHR0cDovL2NybC51c2VydHJ1\n"
      "c3QuY29tL1VTRVJUcnVzdFJTQUNlcnRpZmljYXRpb25BdXRob3JpdHkuY3JsMHYG\n"
      "CCsGAQUFBwEBBGowaDA/BggrBgEFBQcwAoYzaHR0cDovL2NydC51c2VydHJ1c3Qu\n"
      "Y29tL1VTRVJUcnVzdFJTQUFkZFRydXN0Q0EuY3J0MCUGCCsGAQUFBzABhhlodHRw\n"
      "Oi8vb2NzcC51c2VydHJ1c3QuY29tMA0GCSqGSIb3DQEBDAUAA4ICAQAVDwoIzQDV\n"
      "ercT0eYqZjBNJ8VNWwVFlQOtZERqn5iWnEVaLZZdzxlbvz2Fx0ExUNuUEgYkIVM4\n"
      "YocKkCQ7hO5noicoq/DrEYH5IuNcuW1I8JJZ9DLuB1fYvIHlZ2JG46iNbVKA3ygA\n"
      "Ez86RvDQlt2C494qqPVItRjrz9YlJEGT0DrttyApq0YLFDzf+Z1pkMhh7c+7fXeJ\n"
      "qmIhfJpduKc8HEQkYQQShen426S3H0JrIAbKcBCiyYFuOhfyvuwVCFDfFvrjADjd\n"
      "4jX1uQXd161IyFRbm89s2Oj5oU1wDYz5sx+hoCuh6lSs+/uPuWomIq3y1GDFNafW\n"
      "+LsHBU16lQo5Q2yh25laQsKRgyPmMpHJ98edm6y2sHUabASmRHxvGiuwwE25aDU0\n"
      "2SAeepyImJ2CzB80YG7WxlynHqNhpE7xfC7PzQlLgmfEHdU+tHFeQazRQnrFkW2W\n"
      "kqRGIq7cKRnyypvjPMkjeiV9lRdAM9fSJvsB3svUuu1coIG1xxI1yegoGM4r5QP4\n"
      "RGIVvYaiI76C0djoSbQ/dkIUUXQuB8AL5jyH34g3BZaaXyvpmnV4ilppMXVAnAYG\n"
      "ON51WhJ6W0xNdNJwzYASZYH+tmCWI+N60Gv2NNMGHwMZ7e9bXgzUCZH5FaBFDGR5\n"
      "S9VWqHB73Q+OyIVvIbKYcSc2w/aSuFKGSA==\n"
      "-----END CERTIFICATE-----";
};

}  // namespace hela::eillance
