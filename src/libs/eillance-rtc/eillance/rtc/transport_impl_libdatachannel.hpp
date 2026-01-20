#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/event_source.h"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.h"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/transport/transport.hpp"
#include "eillance/transport/transport_delegate.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/invariant.hpp"
#include "eillance/utils/memory.h"

namespace eillance::rtc {

class RtcTrack : public transport::ITrack {
  std::shared_ptr<::rtc::Track> track_;
  std::shared_ptr<::rtc::RtcpSrReporter> reporter_;
  std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config_;

public:
  RtcTrack(std::shared_ptr<::rtc::Track> track, std::shared_ptr<::rtc::RtcpSrReporter> reporter, std::shared_ptr<::rtc::RtpPacketizationConfig> rtp_config) : track_(std::move(track)), reporter_(std::move(reporter)), rtp_config_(std::move(rtp_config)) {}

  void SendSample(const std::vector<std::byte>& sample, uint64_t sample_time) override {
    const auto elapsed_seconds = static_cast<double>(sample_time) / Time::uS;
    const auto elapsed_timestamp = rtp_config_->secondsToTimestamp(elapsed_seconds);
    rtp_config_->timestamp = rtp_config_->startTimestamp + elapsed_timestamp;
    if (!sample.empty()) { track_->send(sample); }
  }

  [[nodiscard]] auto GetTrack() const { return track_; }
};

namespace {

auto ToEillanceState(::rtc::PeerConnection::State state) -> transport::PeerConnectionState {
  switch (state) {
    case ::rtc::PeerConnection::State::New:
      return transport::PeerConnectionState::newConnection;
    case ::rtc::PeerConnection::State::Connecting:
      return transport::PeerConnectionState::connecting;
    case ::rtc::PeerConnection::State::Connected:
      return transport::PeerConnectionState::connected;
    case ::rtc::PeerConnection::State::Disconnected:
      return transport::PeerConnectionState::disconnected;
    case ::rtc::PeerConnection::State::Failed:
      return transport::PeerConnectionState::failed;
    case ::rtc::PeerConnection::State::Closed:
      return transport::PeerConnectionState::closed;
  }
  return transport::PeerConnectionState::closed;  // Should not happen
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

  void SetDelegate(eillance::transport::TransportDelegate* delegate) override { delegate_ = delegate; }

  void SetStatusCallback(const StatusCallbackFunction& callback) override { status_callback_ = std::make_shared<StatusCallbackFunction>(callback); }

  auto GetServiceConnectionState() const -> ServiceConnectionState { return serviceConnectionState_; }

  void SetName(const std::string& device_name) override { deviceName_ = device_name; }

  void SetId(const ContactID& id) override {
    assert(!is_logged_in_);
    id_ = id;
    if (deviceName_.empty()) { deviceName_ = id_.ToString(); }
  }

  void SetSignalingUrl(const std::string& url) override { signalingServerUrl_ = url; }

  void Start() override {
    assert(delegate_ != nullptr && "RtcTransport delegate must be set before starting");
    assert(!id_.IsEmpty());
    LGI << "The local ID is: " << id_;
    terminate_requested_ = false;
    ConnectWebSocket();
  }

  void Terminate() override {
    terminate_requested_ = true;
    if (ws_ != nullptr) {
      if (ws_->isOpen()) { ws_->close(); }
      ws_->resetCallbacks();
    }
    ReportServiceConnectionState(ServiceConnectionState::disconnected);
  }

  void Reconnect() override {
    if (ws_ != nullptr) {
      LGI << "Reconnect requested. Closing current WebSocket connection to re-establish.";
      if (ws_->isOpen()) { ws_->close(); }
    }
  }

  void AddPeer(const ContactID& peer_id, const std::shared_ptr<Peer>& peer) override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.emplace(peer_id, peer);
  }

  auto GetPeer(const ContactID& peer_id) const -> std::shared_ptr<Peer> override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }

  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    return peers_;
  }

  auto GetPeerConnectionState(const ContactID& peer_id) const -> eillance::transport::PeerConnectionState override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    const auto it = peers_.find(peer_id);
    if (it != peers_.end() && it->second && it->second->data_peer_connection) {
      return ToEillanceState(std::static_pointer_cast<::rtc::PeerConnection>(it->second->data_peer_connection)->state());
    }
    return eillance::transport::PeerConnectionState::disconnected;
  }

  void RemovePeer(const ContactID& peer_id) override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.erase(peer_id);
  }

  void SendSignalingMessage(const Json& message) const override { WsSendMessage(message); }

  void SendPeerDataChannelMessage(const ContactID& peer_id, const Json& message) const override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    auto it = peers_.find(peer_id);
    if (it == peers_.end() || !it->second->data_channel) { return; }

    auto dc = std::static_pointer_cast<::rtc::DataChannel>(it->second->data_channel);
    if (dc && dc->isOpen()) { dc->send(message.dump()); }
  }

  void AddToStream(const std::shared_ptr<Peer>& peer, bool isAddingVideo) const override {
    if (peer->GetState() == Peer::State::waiting) {
      peer->SetState(isAddingVideo ? Peer::State::waitingForAudio : Peer::State::waitingForVideo);
    } else if ((peer->GetState() == Peer::State::waitingForAudio && !isAddingVideo) || (peer->GetState() == Peer::State::waitingForVideo && isAddingVideo)) {
      assert(peer->video != nullptr && peer->audio != nullptr);
      peer->SetState(Peer::State::ready);
    }

    if (peer->GetState() == Peer::State::ready && (delegate_ != nullptr)) {
      delegate_->OnPeerReady(peer);
    }
  }

  void CreatePeerConnection(const ContactID& peer_id, const std::string& context) {
    auto peer_connection = std::make_shared<::rtc::PeerConnection>(RtcConfiguration::Config());

    peer_connection->onStateChange([this, peer_id, context, pc_sptr = peer_connection](::rtc::PeerConnection::State state) {
      std::string state_str = "unknown";
      switch (state) {
        case ::rtc::PeerConnection::State::New:
          state_str = "New";
          break;
        case ::rtc::PeerConnection::State::Connecting:
          state_str = "Connecting";
          break;
        case ::rtc::PeerConnection::State::Connected:
          state_str = "Connected";
          break;
        case ::rtc::PeerConnection::State::Disconnected:
          state_str = "Disconnected";
          break;
        case ::rtc::PeerConnection::State::Failed:
          state_str = "Failed";
          break;
        case ::rtc::PeerConnection::State::Closed:
          state_str = "Closed";
          break;
      }
      LGI << "Peer " << peer_id << " (" << context << ") state changed to " << state_str << " (" << static_cast<int>(state) << ")";
      ReportPeerStateChange(peer_id, EventSource::peerConnectionState, static_cast<int>(ToEillanceState(state)));

      if (state == ::rtc::PeerConnection::State::Failed || state == ::rtc::PeerConnection::State::Disconnected) {
        if (auto peer = GetPeer(peer_id)) {
          LGI << "Closing failed/disconnected " << context << " peer connection for " << peer_id;
          if (context == "media" && peer->has_media_peer_connection) { ClosePeerConnection(peer_id, "media"); }
        }
      } else if (state == ::rtc::PeerConnection::State::Closed) {
        auto current_peer = GetPeer(peer_id);
        if (current_peer) {
          auto current_pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(current_peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(current_peer->media_peer_connection);

          if (current_pc == pc_sptr) {
            LGI << "Active peer connection for " << peer_id << " (" << context << ") closed. Client layer is responsible for removal.";
          } else {
            LGI << "Ignoring 'Closed' event from stale peer connection for " << peer_id << " (" << context << ").";
          }
        } else {
          LGI << "Ignoring 'Closed' event for already removed peer " << peer_id << " (" << context << ").";
        }
      }
    });

    peer_connection->onLocalDescription([this, peer_id, context](const ::rtc::Description& description) {
      LGD << "Local SDP (" << description.typeString() << ") for " << peer_id << " context " << context << ":\n"
          << std::string(description);
      Json message = {{"type", description.typeString()}, {"sdp", std::string(description)}, {"context", context}};
      message["to"] = peer_id;
      WsSendMessage(message);
    });

    peer_connection->onLocalCandidate([this, peer_id, context](const ::rtc::Candidate& candidate) {
      Json message = {{"type", "candidate"}, {"candidate", std::string(candidate)}, {"sdpMid", candidate.mid()}, {"context", context}};
      message["to"] = peer_id;
      WsSendMessage(message);
    });

    peer_connection->onIceStateChange([this, peer_id](::rtc::PeerConnection::IceState state) {
      LGI << "ICE State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::iceState, static_cast<int>(state));
    });

    peer_connection->onGatheringStateChange([this, peer_id](::rtc::PeerConnection::GatheringState state) {
      LGI << "Gathering State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::gatheringState, static_cast<int>(state));
    });

    peer_connection->onSignalingStateChange([this, peer_id](::rtc::PeerConnection::SignalingState state) {
      LGI << "Signaling State: " << static_cast<int>(state);
      ReportPeerStateChange(peer_id, EventSource::signalingState, static_cast<int>(state));
    });

    peer_connection->onTrack([this, peer_id](const std::shared_ptr<::rtc::Track>& track) {
      assert(delegate_);
      if (track->mid().find("video") != std::string::npos) {
        track->onMessage([this, peer_id](::rtc::message_variant var) {
          if (std::holds_alternative<::rtc::binary>(var)) { delegate_->OnFrameReceived(peer_id, std::get<::rtc::binary>(var)); }
        });
      }
    });

    if (context == "data") {
      peer_connection->onDataChannel([this, peer_id](const std::shared_ptr<::rtc::DataChannel>& dc) {
        LGI << "Data channel received from " << peer_id;
        auto peer = GetPeer(peer_id);
        if (!peer) {
          LGE << "Peer object not found for incoming data channel from " << peer_id;
          return;
        }

        dc->onOpen([this, peer_id]() { delegate_->OnDataChannelOpen(peer_id); });
        if (peer->on_data_channel_message) {
          dc->onMessage(nullptr, [cb = peer->on_data_channel_message](const std::string& msg) { cb(msg); });
        } else {
          LGE << "No on_message handler found for peer " << peer_id << " on new data channel.";
        }
        peer->data_channel = dc;
      });
    }

    auto peer = GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<Peer>(peer_id);
      AddPeer(peer_id, peer);
    }

    if (context == "data") {
      peer->data_peer_connection = peer_connection;
    } else {
      peer->media_peer_connection = peer_connection;
    }
  }

  void CreateDataPeerConnection(const ContactID& peer_id) override { CreatePeerConnection(peer_id, "data"); }
  void CreateMediaPeerConnection(const ContactID& peer_id) override { CreatePeerConnection(peer_id, "media"); }

  void CreateDataChannel(const ContactID& peer_id) override {
    auto peer = GetPeer(peer_id);
    if (!peer || !peer->data_peer_connection) { return; }
    auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection);

    auto dc = pc->createDataChannel("data");

    dc->onOpen([this, peer_id]() { delegate_->OnDataChannelOpen(peer_id); });
    if (peer->on_data_channel_message) {
      dc->onMessage(nullptr, [cb = peer->on_data_channel_message](const std::string& msg) { cb(msg); });
    }

    peer->data_channel = dc;
  }

  void ClosePeerConnection(const ContactID& peer_id, const std::string& context) override {
    auto peer = GetPeer(peer_id);
    if (!peer) { return; }
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);
    if (pc) { pc->close(); }
  }

  void CreateOffer(const ContactID& peer_id, const std::string& context) override {
    auto peer = GetPeer(peer_id);
    if (!peer) { return; }
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);
    if (pc) { pc->setLocalDescription(); }
  }

  void CreateAnswer(const ContactID& peer_id, const std::string& context) override {
    auto peer = GetPeer(peer_id);
    if (!peer) { return; }
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);
    if (pc) { pc->setLocalDescription(::rtc::Description::Type::Answer); }
  }

  void SetRemoteDescription(const ContactID& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {
    auto peer = GetPeer(peer_id);
    if (!peer) { return; }
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);
    if (pc) { pc->setRemoteDescription(::rtc::Description(sdp, type)); }
  }
  void AddRemoteCandidate(const ContactID& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {
    auto peer = GetPeer(peer_id);
    if (!peer) { return; }
    auto pc = (context == "data") ? std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection) : std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);
    if (pc) { pc->addRemoteCandidate(::rtc::Candidate(candidate, mid)); }
  }

private:
  void ConnectWebSocket() {
    if (ws_ != nullptr && ws_->isOpen()) {
      LGW << "WebSocket is already open, skipping connection attempt.";
      return;
    }
    if (terminate_requested_) {
      LGI << "Terminate requested, aborting connection attempt.";
      return;
    }

    auto ws_config = ::rtc::WebSocketConfiguration{
        .disableTlsVerification = true,
    };

    ReportServiceConnectionState(ServiceConnectionState::establishing);
    ws_ = std::make_unique<::rtc::WebSocket>(ws_config);
    ws_->onOpen([this]() { OnWebSocketOpen(); });
    ws_->onClosed([this]() { OnWebSocketClosed(); });
    ws_->onError([this](const std::string& error) { OnWebSocketError(error); });
    ws_->onMessage(nullptr, [this](const std::string& data) { OnWebSocketMessage(data); });

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

    // Flush any messages that were queued while the connection was opening
    const std::lock_guard<std::mutex> lock(messageQueueMutex_);
    if (!message_queue_.empty()) {
      LGI << "Flushing " << message_queue_.size() << " queued signaling messages.";
      for (const auto& msg: message_queue_) {
        ws_->send(msg);
      }
      message_queue_.clear();
    }
  }

  void OnWebSocketClosed() {
    LGW << "WebSocket connection closed";
    is_logged_in_ = false;

    {
      const std::lock_guard<std::mutex> lock(messageQueueMutex_);
      message_queue_.clear();
    }

    auto peers_to_close = GetPeers();
    for (const auto& [id, peer]: peers_to_close) {
      if (peer && peer->data_peer_connection) { std::static_pointer_cast<::rtc::PeerConnection>(peer->data_peer_connection)->close(); }
    }

    ReportServiceConnectionState(ServiceConnectionState::disconnected);
    ScheduleReconnect();
  }

  void OnWebSocketError(const std::string& error) {
    LGE << "WebSocket connection failed: " << error.c_str();
    is_logged_in_ = false;
    ReportServiceConnectionState(ServiceConnectionState::failure);
  }

  void ScheduleReconnect() {
    if (terminate_requested_) { return; }

    connectionAttempt_++;
    LGI << "Attempting to reconnect in " << waitBeforeReconnect_ << "s (attempt " << connectionAttempt_ << ")";
    DispatchQueue::MainThread().DispatchAfter(std::chrono::seconds(waitBeforeReconnect_), [this]() { ConnectWebSocket(); });

    if (connectionAttempt_ < maxConnectionAttempts) { waitBeforeReconnect_ = std::min(waitBeforeReconnect_ * 2, maxWaitBeforeReconnect); }
  }

  void OnWebSocketMessage(const std::string& data) {
    assert(delegate_);
    const auto message = Json::parse(data);
    const auto itr = message.find("from");
    if (itr == message.end()) {
      DispatchQueue::MainThread().Dispatch([this, message]() { OnSystemMessage(message); });
    } else {
      DispatchQueue::MainThread().Dispatch([this, message]() { delegate_->ProcessPeerMessage(message); });
    }
  }

  void ReportServiceConnectionState(ServiceConnectionState state) const {
    serviceConnectionState_ = state;
    if (status_callback_) { (*status_callback_)(ContactID(), EventSource::system, static_cast<int>(state)); }
  }

  void WsSendMessage(const Json& message) const {
    if (!ws_ || !ws_->isOpen()) {
      const std::lock_guard<std::mutex> lock(messageQueueMutex_);
      message_queue_.push_back(message.dump());
      LGW << "WebSocket not open. Queued message: " << message.dump();
      return;
    }
    try {
      ws_->send(message.dump());
    } catch (const std::exception& e) {
      LGE << "Can't send a WebSocket message due to the exception: " << e.what();
    }
  }

  void Login() {
    const Json message = {
        {"type", "login"},
        {"id", id_},
    };
    WsSendMessage(message);
  }

  void OnSystemMessage(const Json& message) {
    assert(delegate_);
    const auto itr = message.find("type");
    if (itr == message.end()) { return; }
    const auto type = itr->get<std::string>();

    if (type == "error") {
      const auto error = message.value("error", "unknown");
      LGE << "Signaling error received: " << error;
      if (const auto for_peer_it = message.find("for_peer"); for_peer_it != message.end()) {
        delegate_->OnSignalingErrorForPeer(for_peer_it->get<ContactID>(), error);
      } else {
        if (error == "auth_failed") {
          Terminate();
        }
        ReportServiceConnectionState(ServiceConnectionState::failure);
      }
    } else {
      delegate_->OnSystemMessage(type, message);
    }
  }

  void ReportPeerStateChange(const ContactID& peer_id, const EventSource source, const int state) const {
    if (status_callback_) {
      (*status_callback_)(peer_id, source, state);
    }
  }

public:
  auto AddVideo(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<transport::ITrack> override {
    const auto peer = GetPeer(peer_id);
    if (!peer || !peer->media_peer_connection) {
      return nullptr;
    }
    const auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);

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

  auto AddAudio(const ContactID& peer_id, const std::function<void(void)>& on_open) -> std::shared_ptr<transport::ITrack> override {
    const auto peer = GetPeer(peer_id);
    if (!peer || !peer->media_peer_connection) {
      return nullptr;
    }
    const auto pc = std::static_pointer_cast<::rtc::PeerConnection>(peer->media_peer_connection);

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
  std::shared_ptr<StatusCallbackFunction> status_callback_{};

  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_{};
  mutable std::mutex peersMutex_;

  std::string signalingServerUrl_;
  transport::TransportDelegate* delegate_{nullptr};

  class RtcConfiguration {
  public:
    static auto Config() -> ::rtc::Configuration& {
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
};

}  // namespace eillance::rtc
