#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>
#include <utility>

#include "eillance/events/event_source.h"
#include "eillance/events/service_connection_state.h"
#include "eillance/logging/logging.h"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/memory.h"

namespace eillance::rtc {

class RtcTrack : public transport::ITrack {
  std::shared_ptr<::rtc::Track> track_;
  std::shared_ptr<::rtc::RtcpSrReporter> reporter_;
  std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config_;

public:
  RtcTrack(std::shared_ptr<::rtc::Track> track,
           std::shared_ptr<::rtc::RtcpSrReporter> reporter,
           std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config)
      : track_(std::move(track)), reporter_(std::move(reporter)), rtp_config_(std::move(rtp_config)) {}

  void SendSample(const std::vector<std::byte>& sample, uint64_t sample_time) override {
    const auto elapsed_seconds = static_cast<double>(sample_time) / Time::uS;
    const auto elapsed_timestamp = rtp_config_->secondsToTimestamp(elapsed_seconds);
    rtp_config_->timestamp = rtp_config_->startTimestamp + elapsed_timestamp;
    if (!sample.empty()) {
      track_->send(sample);
    }
  }

  auto getTrack() const { return track_; }
};

}  // namespace eillance::rtc

namespace eillance::rtc {
namespace {
eillance::transport::PeerConnectionState toEillanceState(::rtc::PeerConnection::State state) {
  switch (state) {
    case ::rtc::PeerConnection::State::New:
      return eillance::transport::PeerConnectionState::New;
    case ::rtc::PeerConnection::State::Connecting:
      return eillance::transport::PeerConnectionState::Connecting;
    case ::rtc::PeerConnection::State::Connected:
      return eillance::transport::PeerConnectionState::Connected;
    case ::rtc::PeerConnection::State::Disconnected:
      return eillance::transport::PeerConnectionState::Disconnected;
    case ::rtc::PeerConnection::State::Failed:
      return eillance::transport::PeerConnectionState::Failed;
    case ::rtc::PeerConnection::State::Closed:
      return eillance::transport::PeerConnectionState::Closed;
  }
  return eillance::transport::PeerConnectionState::Closed;  // Should not happen
}
}  // namespace

class RtcTransport final : public transport::Transport::TransportInterface {
public:
  using Json = nlohmann::json;
  using Peer = transport::Peer;  // Alias for convenience
  using StatusCallbackFunction = eillance::transport::Transport::StatusCallbackFunction;

  RtcTransport() = default;
  RtcTransport(RtcTransport&&) = delete;
  RtcTransport(const RtcTransport&) = delete;
  auto operator=(RtcTransport&&) -> RtcTransport& = delete;
  auto operator=(const RtcTransport&) -> RtcTransport& = delete;
  ~RtcTransport() override = default;

  void setDelegate(eillance::transport::TransportDelegate* delegate) override {
    delegate_ = delegate;
  }

  void setStatusCallback(const StatusCallbackFunction& callback) override {
    status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
  }

  auto getServiceConnectionState() const -> ServiceConnectionState {
    return service_connection_state_;
  }

  void setName(const std::string& device_name) override {
    device_name_ = device_name;
  }

  void setId(const std::string& id) override {
    assert(!is_logged_in_);
    id_ = id;
    if (device_name_.empty()) {
      device_name_ = id_;
    }
  }

  void start() override {
    assert(delegate_ != nullptr && "RtcTransport delegate must be set before starting");
    assert(!id_.empty());
    LOGI << "The local ID is: " << id_.data();
    terminate_requested_ = false;
    connectWebSocket();
  }

  void terminate() override {
    terminate_requested_ = true;
    if (ws_ != nullptr) {
      if (ws_->isOpen()) {
        ws_->close();
      }
      ws_->resetCallbacks();
    }
    reportServiceConnectionState(ServiceConnectionState::disconnected);
  }

  void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.emplace(std::string(peer_id), peer);
  }

  auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }

  auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_;
  }

  auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    const auto it = peers_.find(peer_id);
    if (it != peers_.end() && it->second && it->second->data_peer_connection_) {
      return toEillanceState(std::static_pointer_cast<::rtc::PeerConnection>(it->second->data_peer_connection_)->state());
    }
    return eillance::transport::PeerConnectionState::Disconnected;
  }

  void removePeer(const std::string& peer_id) override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
  }

  void sendSignalingMessage(const Json& message) const override {
    wsSendMessage(message);
  }

  void sendPeerDataChannelMessage(const std::string& peer_id, const Json& message) const override {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it == peers_.end() || !it->second->data_channel_) return;

    auto dc = std::static_pointer_cast<::rtc::DataChannel>(it->second->data_channel_);
    if (dc && dc->isOpen()) {
      dc->send(message.dump());
    }
  }

  void addToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override {
    if (peer->GetState() == Peer::State::waiting) {
      peer->SetState(isAddingVideo ? Peer::State::waitingForAudio : Peer::State::waitingForVideo);
    } else if ((peer->GetState() == Peer::State::waitingForAudio && !isAddingVideo) ||
               (peer->GetState() == Peer::State::waitingForVideo && isAddingVideo)) {
      assert(peer->video_ != nullptr && peer->audio_ != nullptr);
      peer->SetState(Peer::State::ready);
    }

    if (peer->GetState() == Peer::State::ready && delegate_) {
      delegate_->onPeerReady(peer);
    }
  }

  void createPeerConnection(const std::string& peer_id, const std::string& context) {
    auto peer_connection = std::make_shared<::rtc::PeerConnection>(RtcConfiguration::config());

    peer_connection->onStateChange([this, peer_id, context, pc_sptr = peer_connection](::rtc::PeerConnection::State state) {
      std::string stateStr = "unknown";
      switch (state) {
        case ::rtc::PeerConnection::State::New:
          stateStr = "New";
          break;
        case ::rtc::PeerConnection::State::Connecting:
          stateStr = "Connecting";
          break;
        case ::rtc::PeerConnection::State::Connected:
          stateStr = "Connected";
          break;
        case ::rtc::PeerConnection::State::Disconnected:
          stateStr = "Disconnected";
          break;
        case ::rtc::PeerConnection::State::Failed:
          stateStr = "Failed";
          break;
        case ::rtc::PeerConnection::State::Closed:
          stateStr = "Closed";
          break;
      }
      LOGI << "Peer " << peer_id << " (" << context << ") state changed to " << stateStr << " (" << static_cast<int>(state) << ")";
      reportPeerStateChange(peer_id, EventSource::peerConnectionState, static_cast<int>(toEillanceState(state)));

      if (state == ::rtc::PeerConnection::State::Failed ||
          state == ::rtc::PeerConnection::State::Disconnected) {
        if (auto peer = getPeer(peer_id)) {
          LOGI << "Closing failed/disconnected " << context << " peer connection for " << peer_id;
          if (context == "media" && peer->has_media_peer_connection_) closePeerConnection(peer_id, "media");
        }
      } else if (state == ::rtc::PeerConnection::State::Closed) {
        auto current_peer = getPeer(peer_id);
        if (current_peer) {
          auto current_pc = (context == "data")
                                ? std::static_pointer_cast<::rtc::PeerConnection>(current_peer->data_peer_connection_)
                                : std::static_pointer_cast<::rtc::PeerConnection>(current_peer->media_peer_connection_);

          if (current_pc == pc_sptr) {
            LOGI << "Active peer connection for " << peer_id << " (" << context << ") closed. Client layer is responsible for removal.";
          } else {
            LOGI << "Ignoring 'Closed' event from stale peer connection for " << peer_id << " (" << context << ").";
          }
        } else {
          LOGI << "Ignoring 'Closed' event for already removed peer " << peer_id << " (" << context << ").";
        }
      }
    });

    peer_connection->onLocalDescription([this, peer_id, context](const ::rtc::Description& description) {
      LOGD << "Local SDP (" << description.typeString() << ") for " << peer_id << " context " << context << ":\n"
           << std::string(description);
      Json message = {{"type", description.typeString()},
                      {"sdp", std::string(description)},
                      {"context", context}};
      message["to"] = peer_id;
      wsSendMessage(message);
    });

    peer_connection->onLocalCandidate([this, peer_id, context](const ::rtc::Candidate& candidate) {
      Json message = {{"type", "candidate"},
                      {"candidate", std::string(candidate)},
                      {"sdpMid", candidate.mid()},
                      {"context", context}};
      message["to"] = peer_id;
      wsSendMessage(message);
    });

    peer_connection->onIceStateChange([this, peer_id](::rtc::PeerConnection::IceState state) {
      LOGI << "ICE State: " << static_cast<int>(state);
      reportPeerStateChange(peer_id, EventSource::iceState, static_cast<int>(state));
    });

    peer_connection->onGatheringStateChange([this, peer_id](::rtc::PeerConnection::GatheringState state) {
      LOGI << "Gathering State: " << static_cast<int>(state);
      reportPeerStateChange(peer_id, EventSource::gatheringState, static_cast<int>(state));
    });

    peer_connection->onSignalingStateChange([this, peer_id](::rtc::PeerConnection::SignalingState state) {
      LOGI << "Signaling State: " << static_cast<int>(state);
      reportPeerStateChange(peer_id, EventSource::signalingState, static_cast<int>(state));
    });

    peer_connection->onTrack([this, peer_id](const std::shared_ptr<::rtc::Track>& track) {
      assert(delegate_);
      if (track->mid().find("video") != std::string::npos) {
        track->onMessage([this, peer_id](::rtc::message_variant var) {
          if (std::holds_alternative<::rtc::binary>(var)) {
            delegate_->onFrameReceived(peer_id, std::get<::rtc::binary>(var));
          }
        });
      }
    });

    if (context == "data") {
      peer_connection->onDataChannel([this, peer_id](std::shared_ptr<::rtc::DataChannel> dc) {
        LOGI << "Data channel received from " << peer_id;
        auto peer = getPeer(peer_id);
        if (!peer) return;

        dc->onOpen([peer, peer_id]() {
          LOGI << "Incoming data channel with " << peer_id << " opened.";
          peer->data_channel_is_open_ = true;
        });

        if (peer->on_data_channel_message_) {
          dc->onMessage(nullptr, [cb = peer->on_data_channel_message_](const std::string& msg) {
            cb(msg);
          });
        } else {
          LOGE << "No on_message handler found for peer " << peer_id << " on new data channel.";
        }
        peer->data_channel_ = dc;
      });
    }

    auto peer = getPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      addPeer(peer_id, peer);
    }

    if (context == "data") {
      peer->data_peer_connection_ = peer_connection;
    } else {
      peer->media_peer_connection_ = peer_connection;
    }
  }

  void createDataPeerConnection(const std::string& peer_id) override { createPeerConnection(peer_id, "data"); }
  void createMediaPeerConnection(const std::string& peer_id) override { createPeerConnection(peer_id, "media"); }

  void createDataChannel(const std::string& peer_id) override {
    auto peer = getPeer(peer_id);
    if (!peer || !peer->data_peer_connection_) return;
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_);

    auto dc = pc->createDataChannel("data");

    dc->onOpen([this, peer_id, peer]() {
      LOGI << "Data channel with " << peer_id << " opened.";
      peer->data_channel_is_open_ = true;
      auto my_pub_key = peer->session_cipher_->GetPublicKey();
      const Json key_exchange_message = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}};
      sendPeerDataChannelMessage(peer_id, key_exchange_message);
    });

    if (peer->on_data_channel_message_) {
      dc->onMessage(nullptr, [cb = peer->on_data_channel_message_](const std::string& msg) {
        cb(msg);
      });
    }

    peer->data_channel_ = dc;
  }

  void closePeerConnection(const std::string& peer_id, const std::string& context) override {
    auto peer = getPeer(peer_id);
    if (!peer) return;
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)
                                  : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);
    if (pc) pc->close();
  }

  void createOffer(const std::string& peer_id, const std::string& context) override {
    auto peer = getPeer(peer_id);
    if (!peer) return;
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)
                                  : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);
    if (pc) pc->setLocalDescription();
  }

  void createAnswer(const std::string& peer_id, const std::string& context) override {
    auto peer = getPeer(peer_id);
    if (!peer) return;
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)
                                  : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);
    if (pc) pc->setLocalDescription(::rtc::Description::Type::Answer);
  }

  void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {
    auto peer = getPeer(peer_id);
    if (!peer) return;
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)
                                  : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);
    if (pc) pc->setRemoteDescription(::rtc::Description(sdp, type));
  }
  void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {
    auto peer = getPeer(peer_id);
    if (!peer) return;
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)
                                  : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);
    if (pc) pc->addRemoteCandidate(::rtc::Candidate(candidate, mid));
  }

private:
  void connectWebSocket() {
    if (ws_ != nullptr && ws_->isOpen()) {
      LOGW << "WebSocket is already open, skipping connection attempt.";
      return;
    }
    if (terminate_requested_) {
      LOGI << "Terminate requested, aborting connection attempt.";
      return;
    }

    auto ws_config = ::rtc::WebSocketConfiguration{
        .disableTlsVerification = true,
    };

    reportServiceConnectionState(ServiceConnectionState::establishing);
    ws_ = std::make_unique<::rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() { onWebSocketOpen(); });
    ws_->onClosed([this]() { onWebSocketClosed(); });
    ws_->onError([this](const std::string& error) { onWebSocketError(error); });
    ws_->onMessage(nullptr, [this](const std::string& data) { onWebSocketMessage(data); });

    LOGI << "Connecting to signaling server: " << signalingServerUrl;
    ws_->open(signalingServerUrl);
  }

  void onWebSocketOpen() {
    LOGI << "WebSocket connected, signaling ready";
    connection_attempt_ = 0;
    wait_before_reconnect_ = minWaitBeforeReconnect;
    login();

    // Flush any messages that were queued while the connection was opening
    const std::lock_guard<std::mutex> lock(message_queue_mutex_);
    if (!message_queue_.empty()) {
      LOGI << "Flushing " << message_queue_.size() << " queued signaling messages.";
      for (const auto& msg: message_queue_) {
        ws_->send(msg);
      }
      message_queue_.clear();
    }
  }

  void onWebSocketClosed() {
    LOGW << "WebSocket connection closed";
    is_logged_in_ = false;

    {
      const std::lock_guard<std::mutex> lock(message_queue_mutex_);
      message_queue_.clear();
    }

    auto peers_to_close = getPeers();
    for (const auto& [id, peer]: peers_to_close) {
      if (peer && peer->data_peer_connection_) {
        std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection_)->close();
      }
    }

    reportServiceConnectionState(ServiceConnectionState::disconnected);
    scheduleReconnect();
  }

  void onWebSocketError(const std::string& error) {
    LOGE << "WebSocket connection failed: " << error.c_str();
    is_logged_in_ = false;
    reportServiceConnectionState(ServiceConnectionState::failure);
  }

  void scheduleReconnect() {
    if (terminate_requested_) {
      return;
    }

    connection_attempt_++;
    LOGI << "Attempting to reconnect in " << wait_before_reconnect_ << "s (attempt " << connection_attempt_ << ")";
    DispatchQueue::MainThread().DispatchAfter(std::chrono::seconds(wait_before_reconnect_), [this]() {
      connectWebSocket();
    });

    if (connection_attempt_ < maxConnectionAttempts) {
      wait_before_reconnect_ = std::min(wait_before_reconnect_ * 2, maxWaitBeforeReconnect);
    }
  }

  void onWebSocketMessage(const std::string& data) {
    assert(delegate_);
    const auto message = Json::parse(data);
    auto itr = message.find("from");
    if (itr == message.end()) {
      DispatchQueue::MainThread().Dispatch([this, message]() {
        onSystemMessage(message);
      });
    } else {
      DispatchQueue::MainThread().Dispatch([this, message]() {
        delegate_->processPeerMessage(message);
      });
    }
  }

  void reportServiceConnectionState(ServiceConnectionState state) const {
    service_connection_state_ = state;
    if (status_callback_) {
      (*status_callback_)("", EventSource::system, static_cast<int>(state));
    }
  }

  void wsSendMessage(const Json& message) const {
    if (!ws_ || !ws_->isOpen()) {
      const std::lock_guard<std::mutex> lock(message_queue_mutex_);
      message_queue_.push_back(message.dump());
      LOGW << "WebSocket not open. Queued message: " << message.dump();
      return;
    }
    try {
      ws_->send(message.dump());
    } catch (const std::exception& e) {
      LOGE << "Can't send a WebSocket message due to the exception: " << e.what();
    }
  }

  void login() {
    const Json message = {
        {"type", "login"},
        {"id", id_},
    };

    wsSendMessage(message);
    is_logged_in_ = true;
    reportServiceConnectionState(ServiceConnectionState::online);
  }

  void onSystemMessage(Json message) const {
    assert(delegate_);
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();

    if (type == "error") {
      const auto error = message.value("error", "unknown");
      LOGE << "Signaling error received: " << error;
      if (const auto for_peer_it = message.find("for_peer"); for_peer_it != message.end()) {
        delegate_->onSignalingErrorForPeer(for_peer_it->get<std::string>(), error);
      } else {
        reportServiceConnectionState(ServiceConnectionState::failure);
      }
    } else {
      delegate_->processSystemMessage(type, message);
    }
  }

  void reportPeerStateChange(const std::string& peer_id, EventSource source, int state) const {
    if (status_callback_) {
      (*status_callback_)(peer_id, source, static_cast<int>(state));
    }
  }

public:
  auto addVideo(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<transport::ITrack> override {
    auto peer = getPeer(peer_id);
    if (!peer || !peer->media_peer_connection_) return nullptr;
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);

    auto video = ::rtc::Description::Video("video", ::rtc::Description::Direction::SendRecv);
    constexpr auto payload_type = 102;
    constexpr auto ssrc = 10001;
    video.addH264Codec(payload_type);
    video.addSSRC(ssrc, "video-stream", "stream1", "video-label");
    auto track = pc->addTrack(video);
    auto rtp_config = std::make_shared<::rtc::RtpPacketizationConfig>(ssrc, "video-stream", payload_type, ::rtc::H264RtpPacketizer::ClockRate);
    const auto packetizer = std::make_shared<::rtc::H264RtpPacketizer>(::rtc::NalUnit::Separator::StartSequence, rtp_config);
    const auto sr_reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    const auto nack_responder = std::make_shared<::rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    track->setMediaHandler(packetizer);
    track->onOpen(on_open);
    return std::make_shared<RtcTrack>(track, sr_reporter, rtp_config);
  }

  auto addAudio(const std::string& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<transport::ITrack> override {
    auto peer = getPeer(peer_id);
    if (!peer || !peer->media_peer_connection_) return nullptr;
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection_);

    auto audio = ::rtc::Description::Audio("audio", ::rtc::Description::Direction::SendRecv);
    constexpr auto payload_type = 111;
    constexpr auto ssrc = 10002;
    audio.addOpusCodec(payload_type);
    audio.addSSRC(ssrc, "audio-stream", "stream1", "audio-label");
    auto track = pc->addTrack(audio);
    auto rtp_config = std::make_shared<::rtc::RtpPacketizationConfig>(ssrc, "audio-stream", payload_type, ::rtc::OpusRtpPacketizer::DefaultClockRate);
    const auto packetizer = std::make_shared<::rtc::OpusRtpPacketizer>(rtp_config);
    const auto sr_reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    track->setMediaHandler(packetizer);
    track->onOpen(on_open);
    return std::make_shared<RtcTrack>(track, sr_reporter, rtp_config);
  }

private:
  std::string id_{};
  std::string device_name_{id_};
  std::atomic<bool> is_logged_in_{false};
  std::atomic<bool> terminate_requested_{false};

  static constexpr auto minWaitBeforeReconnect = 1U;
  static constexpr auto maxWaitBeforeReconnect = 10U;
  static constexpr auto maxConnectionAttempts = 10U;

  std::unique_ptr<::rtc::WebSocket> ws_{nullptr};
  std::uint32_t connection_attempt_{0U};
  std::uint32_t wait_before_reconnect_{minWaitBeforeReconnect};

  mutable std::vector<std::string> message_queue_;
  mutable std::mutex message_queue_mutex_;

  mutable ServiceConnectionState service_connection_state_{ServiceConnectionState::unknown};
  std::shared_ptr<StatusCallbackFunction> status_callback_{};

  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peers_mutex_;

  eillance::transport::TransportDelegate* delegate_{nullptr};

  class RtcConfiguration {
  public:
    static auto config() -> ::rtc::Configuration& {
      static ::rtc::Configuration rtc_config = CreateRtcConfiguration();
      return rtc_config;
    }

  private:
    static auto CreateRtcConfiguration() -> ::rtc::Configuration {
      ::rtc::Configuration config;
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
  constexpr static auto signalingServerUrl = "ws://10.0.2.2:8000";
#else
  constexpr static auto signalingServerUrl = "ws://127.0.0.1:8000";
#endif
#endif
};

}  // namespace eillance::rtc
