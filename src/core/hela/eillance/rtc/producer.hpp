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

class RtcProducer final : public RtcTransport {
  using Json = nlohmann::json;
  using PeerTrack = std::shared_ptr<Peer::Track>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;

public:
  explicit RtcProducer() {
    av_stream_ = std::make_shared<Stream>(
        std::make_shared<StreamSource>(Capture(std::make_unique<VideoCapture>())),
        std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

    // set callback responsible for sample sending
    av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      SendSampleToReadyPeers(sample, type, sampleTime);
      DispatchQueue::MainThread().Dispatch([this, ws]() {
        if (GetPeers().empty()) {
          // we have no peers, stop the stream
          if (auto stream = ws.lock()) {
            stream->Stop();
          }
        }
      });
    });
  }
  RtcProducer(RtcProducer&&) = delete;
  RtcProducer(const RtcProducer&) = delete;
  RtcProducer& operator=(RtcProducer&&) = delete;
  RtcProducer& operator=(const RtcProducer&) = delete;
  ~RtcProducer() final = default;

  void OnPeerDataChannelConnected(const std::shared_ptr<Peer>& peer) final {
    if (!peer->data_channel_->isOpen()) {
      return;
    }
    const Json message = {
        {"type", "info profile"},
        {"profile", GetDeviceProfileAsJson()},
    };
    peer->data_channel_->send(message.dump());
  }

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
    av_stream_->Start();
  }

  void ProcessPeerMessage(Json message) final {
    auto it = message.find("from");
    if (it == message.end()) {
      return;
    }
    auto peer_id = it->get<std::string>();
    it = message.find("type");
    if (it == message.end()) {
      return;
    }
    auto type = it->get<std::string>();

    if (type == "streamRequest") {
      const auto peer = CreatePeerConnection(peer_id);
      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        AddPeer(peer_id, peer);
      });
    } else if (type == "answer") {
      if (const auto peer = GetPeer(peer_id); peer != nullptr) {
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
    throw std::runtime_error("Unknown message type in Producer's ProcessSystemMessage: " + type);
  }

  void ProcessDataChannelMessage(const std::string& peer_id, Json message) final {
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
        {"kind", "producer"},
        {"profile", GetDeviceProfileAsJson()},
    };
  }

  void Configure(Config config) {
    av_stream_->Video()->Configure(config);
  }

private:
  void SendSampleToReadyPeers(
      const rtc::binary& sample,
      const Stream::Source::Type type,
      const uint64_t sample_time) {
    // get track for given type
    auto get_track_data = [type](const std::shared_ptr<Peer>& peer) {
      return type == Stream::Source::Type::video ? peer->video_ : peer->audio_;
    };

    const auto* stream_type = type == Stream::Source::Type::video ? "video" : "audio";
    // get all peers with Ready state
    for (const auto& id_peer: GetPeers()) {
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

    LOGI << "Sending " << stream_type << " sample with size: " << sample.size() << " to " << peer_id;
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
