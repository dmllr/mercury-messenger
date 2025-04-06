#pragma once

#include <memory>
#include <utility>

#include <nlohmann/json.hpp>
#include <rtc/rtc.h>

#include "hela/eillance/capture/audio_capture.hpp"
#include "hela/eillance/capture/video_capture.hpp"
#include "hela/eillance/device/camera_utils.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/peer.hpp"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/rtc/stream_source.hpp"
#include "hela/eillance/rtc/transport.hpp"
#include "hela/eillance/utils/memory.h"


namespace hela::eillance {

/**
 * @brief RTC streamer implementation
 *
 * Handles streaming audio and video over WebRTC
 */
class RtcStreamer final : public RtcTransportDelegate {
  using Json = nlohmann::json;
  using PeerTrack = std::shared_ptr<Peer::Track>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;

  /// @brief RTC transport delegate
  std::shared_ptr<RtcTransport> transport_;

public:
  explicit RtcStreamer() {
    av_stream_ = std::make_shared<Stream>(
        std::make_shared<StreamSource>(Capture(std::make_unique<VideoCapture>())),
        std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

    transport_ = std::make_shared<RtcTransport>(this);

    // set callback responsible for sending the sample
    av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      SendSampleToPeers(sample, type, sampleTime);
      DispatchQueue::MainThread().Dispatch([this, ws]() {
        if (transport_->GetPeers().empty()) {
          // we have no peers, stop the stream
          if (auto stream = ws.lock()) {
            stream->Stop();
          }
        }
      });
    });
  }

  RtcStreamer(RtcStreamer&&) = delete;
  RtcStreamer(const RtcStreamer&) = delete;
  RtcStreamer& operator=(RtcStreamer&&) = delete;
  RtcStreamer& operator=(const RtcStreamer&) = delete;
  ~RtcStreamer() = default;

  void SetId(const std::string& id) {
    transport_->SetId(id);
  }

  void SetName(const std::string& name) {
    transport_->SetName(name);
  }

  void UpdateAuthToken(const std::string& token) {
    transport_->UpdateAuthToken(token);
  }

  void Start() {
    transport_->Start();
  }

  void StopStreaming() {
    transport_->StopStreaming();
  }

  void Terminate() {
    transport_->Terminate();
  }

  [[maybe_unused]] auto GetServiceConnectionState() {
    return transport_->GetServiceConnectionState();
  }

  void SetStatusCallback(const std::function<void(const std::string& peer_id, EventSource, int)>& callback) {
    transport_->SetStatusCallback(callback);
  }

  static void OnPeerDataChannelConnected(const std::shared_ptr<Peer>& peer) {
    if (!peer->data_channel_->isOpen()) {
      return;
    }
    const Json message = {
        {"type", "profile"},
        {"profile", GetDeviceProfileAsJson()},
    };
    peer->data_channel_->send(message.dump());
  }

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
    av_stream_->Start();
  }

  void ProcessPeerMessage(Json message) final {
    auto from_it = message.find("from");
    if (from_it == message.end()) {
      return;
    }
    auto peer_id = from_it->get<std::string>();
    from_it = message.find("type");
    if (from_it == message.end()) {
      return;
    }
    auto type = from_it->get<std::string>();

    if (type == "stream_request") {
      const auto peer = transport_->CreatePeer(peer_id);
      peer->data_channel_ = CreateDataChannel(peer->peer_connection_, peer_id);

      // Apply audio/video configuration
      auto config_it = message.find("config");
      if (config_it != message.end()) {
        ApplyConfiguration(peer_id, *config_it);
      }

      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        transport_->AddPeer(peer_id, peer);
      });
    } else if (type == "answer") {
      if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr) {
        auto sdp = message["sdp"].get<std::string>();
        auto answer = rtc::Description(sdp, type);
        peer->peer_connection_->setRemoteDescription(answer);
      }
    } else if (type == "offer") {
      // do nothing
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void ProcessSystemMessage(const std::string& type, Json message) final {
    throw std::runtime_error("Unknown message type in Streamer's ProcessSystemMessage: " + type);
  }

  void ProcessDataChannelMessage(const std::string& peer_id, Json message) {
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();
    if (type == "configuration") {
      auto config_it = message.find("config");
      if (config_it != message.end()) {
        ApplyConfiguration(peer_id, *config_it);
      }
    } else if (type == "termination") {
      transport_->RemovePeer(peer_id);
    }
  }

  auto AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> final {
    constexpr auto payload_type = 102;
    constexpr auto ssrc = 10001;
    constexpr auto msid = "cam-NAME-stream";
    constexpr auto cname = "video-stream";
    auto video = rtc::Description::Video(cname, rtc::Description::Direction::SendOnly);
    video.addH264Codec(payload_type);
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);
    // create RTP configuration
    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payload_type, rtc::H264RtpPacketizer::defaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtp_config);
    // add RTCP SR handler
    auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, sr_reporter);
  }

  auto AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> final {
    constexpr auto payload_type = 111;
    constexpr auto ssrc = 10002;
    constexpr auto msid = "cam-NAME-stream";
    constexpr auto cname = "audio-stream";
    auto audio = rtc::Description::Audio(cname, rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(payload_type);
    audio.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(audio);
    // create RTP configuration
    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtp_config);
    // add RTCP SR handler
    auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(sr_reporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, sr_reporter);
  }

  Json GetPublishMessage() final {
    return Json{
        {"kind", "streamer"},
        {"profile", GetDeviceProfileAsJson()},
    };
  }

  void Configure(Config config) {
    av_stream_->Video()->Configure(config);
  }

private:
  void ApplyConfiguration(const std::string& peer_id, const Json& configJson) {
    auto config{Config()};
    auto& video_config = config.video_config;

    auto video_it = configJson.find("video");
    if (video_it != configJson.end() && !video_it->is_null()) {
      auto& jvc = *video_it;

      if (jvc.contains("cameraId") && !jvc["cameraId"].is_null()) {
        video_config.cameraId = jvc["cameraId"].get<int>();
      }

      if (jvc.contains("width") && !jvc["width"].is_null()) {
        video_config.width = jvc["width"].get<std::uint32_t>();
      }

      if (jvc.contains("height") && !jvc["height"].is_null()) {
        video_config.height = jvc["height"].get<std::uint32_t>();
      }

      LOGD << "Applying video configuration from " << peer_id
           << " (cameraId: " << video_config.cameraId
           << ", width: " << video_config.width
           << ", height: " << video_config.height << ")";

      Configure(config);
    }
  }

  auto CreateDataChannel(std::shared_ptr<rtc::PeerConnection> pc, const std::string& peer_id) -> std::shared_ptr<rtc::DataChannel> {
    auto data_channel = pc->createDataChannel("command-channel");
    data_channel->onOpen([this, peer_id, wdc = make_weak_ptr(data_channel)]() {
      if (auto dc = wdc.lock()) {
        dc->send("Ping");
      }
      OnPeerDataChannelConnected(transport_->GetPeer(peer_id));
    });

    data_channel->onMessage(nullptr, [peer_id, wdc = make_weak_ptr(data_channel), this](const std::string& msg) {
      LOGV << "Message from " << peer_id << " received: " << msg;
      if (msg.rfind("Pong", 0) == 0) {  // NOLINT(*-use-starts-ends-with) starts_with is not supported by all compilers
        if (auto dc = wdc.lock()) {
          dc->send("Ping");
        }
        transport_->ReportPeerStateChange(peer_id, EventSource::ping, 1);
        return;
      }

      ProcessDataChannelMessage(peer_id, Json::parse(msg));
    });

    return data_channel;
  }

  void SendSampleToPeers(
      const rtc::binary& sample,
      const Stream::Source::Type type,
      const uint64_t sample_time) {
    // get track for given type
    auto get_track_data = [type](const std::shared_ptr<Peer>& peer) {
      return type == Stream::Source::Type::video ? peer->video_ : peer->audio_;
    };

    const auto* stream_type = type == Stream::Source::Type::video ? "video" : "audio";
    // get all peers with Ready state
    for (const auto& id_peer: transport_->GetPeers()) {
      auto id = id_peer.first;
      auto peer = id_peer.second;
      auto opt_track_data = get_track_data(peer);
      if (peer->GetState() == Peer::State::ready && (opt_track_data != nullptr)) {
        ProcessTrack(id, opt_track_data, sample_time, sample, stream_type);
      }
    }
  }

  static void ProcessTrack(
      const std::string& peer_id,
      const std::shared_ptr<Peer::Track>& track,
      const uint64_t sample_time,
      const rtc::binary& sample,
      const char* stream_type) {
    auto elapsed_seconds = static_cast<double>(sample_time) / Time::uS;
    const auto rtp_config = track->Reporter()->rtpConfig;
    auto elapsed_timestamp = rtp_config->secondsToTimestamp(elapsed_seconds);

    UpdateRtpConfig(elapsed_timestamp, track, rtp_config);

    LOGV << "Sending " << stream_type << " sample with size: " << sample.size() << " to " << peer_id;
    SendSample(sample, stream_type, track);
  }

  static void UpdateRtpConfig(
      const uint32_t elapsed_timestamp,
      const PeerTrack& track,
      const std::shared_ptr<rtc::RtpPacketizationConfig>& rtp_config) {
    // set new timestamp
    rtp_config->timestamp = rtp_config->startTimestamp + elapsed_timestamp;

    // get elapsed time in clock rate from last RTCP sender report
    auto report_elapsed_timestamp = rtp_config->timestamp - track->Reporter()->lastReportedTimestamp();
    // check if last report was at least 1 second ago
    if (rtp_config->timestampToSeconds(report_elapsed_timestamp) > 1) {
      track->Reporter()->setNeedsToReport();
    }
  }

  static void SendSample(const rtc::binary& message, const char* streamType, const PeerTrack& track) {
    bool send = false;
    try {
      // send sample
      if (likely(!message.empty())) {
        send = track->Send(message);
      }
    } catch (const std::exception& e) {
      LOGE << "Exception: unable to send " << streamType << " packet." << e.what();
      send = false;
    }
    if (!send) {
      LOGD << "Unable to send " << streamType << " packet";
    }
  }
};

}  // namespace hela::eillance
