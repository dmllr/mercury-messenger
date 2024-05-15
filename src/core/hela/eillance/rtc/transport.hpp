#pragma once

#include <memory>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/rtc.h>

#include "hela/eillance/common/dispatch_queue.hpp"
#include "hela/eillance/events/event_source.h"
#include "hela/eillance/events/service_state.h"
#include "hela/eillance/events/system_event.h"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/peer.hpp"
#include "hela/eillance/rtc/status.hpp"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/utils/memory.h"


namespace hela::eillance {

template<typename Connection>
class RtcTransport {
  using Json = nlohmann::json;

  const std::string_view id;
  std::string auth_token_;

  static constexpr auto minWaitBeforeReconnect = 1;
  static constexpr auto maxWaitBeforeReconnect = 10;

  Connection connection_{};
  rtc::WebSocketConfiguration ws_config_;
  rtc::WebSocket ws_{ws_config_};
  std::uint32_t connection_attempt_{0};
  std::uint32_t wait_before_reconnect_{minWaitBeforeReconnect};

  mutable Status status_{};
  std::shared_ptr<std::function<void(EventSource, int)>> status_callback_;

public:
  explicit RtcTransport(std::string_view local_id) : id(local_id) {
    ws_config_.caCertificatePemFile = zerosslCaBundle;
  }
  RtcTransport(RtcTransport&&) = delete;
  RtcTransport(const RtcTransport&) = delete;
  auto operator=(RtcTransport&&) -> RtcTransport& = delete;
  auto operator=(const RtcTransport&) -> RtcTransport& = delete;
  ~RtcTransport() = default;

  [[maybe_unused]] void SetStatusCallback(const std::function<void(EventSource, int)>& callback) {
    status_callback_ = std::make_shared<std::function<void(EventSource, int)>>(callback);
  }

  [[maybe_unused]] Status GetStatus() {
    return status_;
  }

  void ReportStatus(const ServiceState state) const {
    status_.service_state = state;
    if (status_callback_) {
      (*status_callback_)(EventSource::ConnectionStatus, static_cast<int>(state));
    }
  }

  void ReportStateChange(const EventSource source, int state) const {
    if (status_callback_) {
      (*status_callback_)(source, static_cast<int>(state));
    }
  }

  void ReportEvent(const SystemEvent event) const {
    if (status_callback_) {
      (*status_callback_)(EventSource::System, static_cast<int>(event));
    }
  }

  void Authenticate() {
    Json message = {
        {"id", id},
        {"type", "authenticate"},
        {"username", "user1"},
        {"password", "password1"}
    };
    ws_.send(message.dump());
  }

  void PublishClient() {
    Json message = {
        {"id", id},
        {"token", auth_token_},
        {"type", "publish_client"},
        {"client_id", id}};
    ws_.send(message.dump());
  }

  void Start() {
    LOGI << "The local ID is: " << id.data();

    connection_attempt_++;
    if (connection_attempt_ <= 1) {
      wait_before_reconnect_ = 0;
    } else if (connection_attempt_ > 1 && connection_attempt_ < 10) {
      wait_before_reconnect_ = minWaitBeforeReconnect;
    } else {
      wait_before_reconnect_ = maxWaitBeforeReconnect;
    }

    ws_.onOpen([this]() {
      LOGI << "WebSocket connected, signaling ready";
      connection_attempt_ = 0;
      ReportStatus(ServiceState::standby);

      Authenticate();
    });

    ws_.onClosed([this]() {
      LOGE << "WebSocket connection closed";
      ReportStatus(ServiceState::failure);

      DispatchQueue::MainThread().dispatch([this]() {
        Start();
      });
      LOGI << "Scheduled WebSocket reconnection in " << wait_before_reconnect_ << "s";
    });

    ws_.onError([this](const std::string& error) {
      LOGE << "WebSocket connection failed: " << error.c_str();
      ReportStatus(ServiceState::failure);
    });

    ws_.onMessage([&](std::variant<rtc::binary, std::string> data) {
      if (!std::holds_alternative<std::string>(data)) {
        return;
      }

      const auto message = Json::parse(std::get<std::string>(data));
      DispatchQueue::MainThread().dispatch([this, message]() {
        OnWSMessage(std::move(message));
      });
    });

    std::this_thread::sleep_for(std::chrono::seconds(wait_before_reconnect_));

    LOGD << "Signaling server URL is " << signalingServerUrl;

    try {
      ws_.open(signalingServerUrl);

      LOGI << "Waiting for signaling to be connected...";
      while (!ws_.isOpen()) {
        if (ws_.isClosed()) {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      connection_attempt_ = 0;
    } catch (const std::exception& e) {
      LOGI << "Error: " << e.what();
      ReportStatus(ServiceState::failure);
      return;
    }
    ReportEvent(SystemEvent::started);
  }

  auto CreateDefaultPeerConnection(const std::string& id) -> std::shared_ptr<rtc::PeerConnection> {
    auto peer_connection = std::make_shared<rtc::PeerConnection>(RtcConfiguration::Config());

    peer_connection->onStateChange([this, id](rtc::PeerConnection::State state) {
      LOGI << "State: " << static_cast<int>(state);
      status_.state = state;
      ReportStateChange(EventSource::PeerConnectionState, static_cast<int>(state));
      if (state == rtc::PeerConnection::State::Connected) {
        ReportStatus(ServiceState::action);
      }
      if (state == rtc::PeerConnection::State::Disconnected ||
          state == rtc::PeerConnection::State::Failed ||
          state == rtc::PeerConnection::State::Closed) {
        DispatchQueue::MainThread().dispatch([this, id]() {
          // remove disconnected peer
          connection_.RemovePeer(id);
        });
        ReportStatus(ServiceState::standby);
      }
    });

    peer_connection->onIceStateChange([this, id](rtc::PeerConnection::IceState state) {
      LOGI << "ICE State: " << static_cast<int>(state);
      ReportStateChange(EventSource::IceState, static_cast<int>(state));
      if (state == rtc::PeerConnection::IceState::Failed) {
        ReportStatus(ServiceState::failure);
      }
    });

    peer_connection->onGatheringStateChange([this, wpc = make_weak_ptr(peer_connection), id](rtc::PeerConnection::GatheringState state) {
      LOGI << "Gathering State: " << static_cast<int>(state);
      status_.gathering_state = state;
      ReportStateChange(EventSource::GatheringState, static_cast<int>(state));
      if (state == rtc::PeerConnection::GatheringState::Complete) {
        // Gathering complete, send answer
        if (auto peer_con = wpc.lock()) {
          auto description = peer_con->localDescription();
          Json message = {
              {"id", id},
              {"token", auth_token_},
              {"type", description->typeString()},
              {"sdp", std::string(description.value())}};
          ws_.send(message.dump());
        }
      }
    });

    peer_connection->onSignalingStateChange([this, id](rtc::PeerConnection::SignalingState state) {
      LOGI << "Signalling State: " << static_cast<int>(state);
      status_.signalling_state = state;
      ReportStateChange(EventSource::SignalingState, static_cast<int>(state));
      if (state == rtc::PeerConnection::SignalingState::HaveLocalOffer) {
        ReportStatus(ServiceState::setup);
      }
    });

    return peer_connection;
  }

  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) {
    if (peer->getState() == Peer::State::Waiting) {
      peer->setState(isAddingVideo ? Peer::State::WaitingForAudio : Peer::State::WaitingForVideo);
    } else if ((peer->getState() == Peer::State::WaitingForAudio && !isAddingVideo) || (peer->getState() == Peer::State::WaitingForVideo && isAddingVideo)) {
      // Audio and video tracks are collected now
      assert(peer->video.has_value() && peer->audio.has_value());
      peer->setState(Peer::State::Ready);
    }

    if (peer->getState() == Peer::State::Ready) {
      connection_.OnPeerReady(peer);
    }
  }

  auto CreateDefaultDataChannel(std::shared_ptr<rtc::PeerConnection> pc, const std::string& id) -> std::shared_ptr<rtc::DataChannel> {
    auto dc = pc->createDataChannel("ping-pong");
    dc->onOpen([id, wdc = make_weak_ptr(dc)]() {
      if (auto dc = wdc.lock()) {
        dc->send("Ping");
      }
    });

    dc->onMessage(nullptr, [id, wdc = make_weak_ptr(dc), this](const std::string& msg) {
      LOGD << "Message from " << id << " received: " << msg;
      if (auto dc = wdc.lock()) {
        dc->send("Ping");
      }
      ReportStateChange(EventSource::Ping, 1);
    });

    return dc;
  }

  void OnWSMessage(Json message) {
    auto it = message.find("id");
    if (it == message.end()) {
      return;
    }
    auto id = it->get<std::string>();
    it = message.find("type");
    if (it == message.end()) {
      return;
    }
    auto type = it->get<std::string>();

    if (type == "streamRequest") {
      std::shared_ptr<Peer> const peer = CreatePeerConnection(id);
      DispatchQueue::MainThread().dispatch([this, id, peer]() {
        connection_.AddPeer(id, std::move(peer));
      });
    } else if (type == "answer") {
      if (const auto peer = connection_.GetPeer(id); peer != nullptr) {
        auto pc = peer->PeerConnection();
        auto sdp = message["sdp"].get<std::string>();
        auto answer = rtc::Description(sdp, type);
        pc->setRemoteDescription(answer);
      }
    } else if (type == "offer") {
      // do nothing
    } else if ((type == "authenticated") && (id == "system")) {
      auth_token_ = message["token"].get<std::string>();
      DispatchQueue::MainThread().dispatch([this]() {
        PublishClient();
      });
    } else if ((type == "client_published") && (id == "system")) {
      // do nothing
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  auto CreatePeerConnection(const std::string& id) -> std::shared_ptr<Peer> {
    auto pc = CreateDefaultPeerConnection(id);

    auto peer = std::make_shared<Peer>(pc);

    peer->video = connection_.AddVideo(
        pc,
        [this, id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, true);
            }
          });
          LOGI << "Video from " << id << " opened";
        });

    peer->audio = connection_.AddAudio(
        pc,
        [this, id, wc = make_weak_ptr(peer)]() {
          DispatchQueue::MainThread().dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, false);
            }
          });
          LOGI << "Audio from " << id << " opened";
        });

    peer->dataChannel = CreateDefaultDataChannel(pc, id);

    pc->setLocalDescription();

    return peer;
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
      config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:80?transport=tcp");
      config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443");
      config.iceServers.emplace_back("turn:49880a0f046c8990b1990b35:qZCNW/57EB8GwO3A@a.relay.metered.ca:443?transport=tcp");
      return config;
    }
  };

//  constexpr static auto signalingServerUrl = "wss://app.hela.me:8000";
  constexpr static auto signalingServerUrl = "ws://localhost:8000";

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
