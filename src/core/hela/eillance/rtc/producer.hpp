#pragma once

#include <memory>
#include <utility>

#include <nlohmann/json.hpp>
#include <rtc/rtc.h>

#include "hela/eillance/capture/audio_capture.hpp"
#include "hela/eillance/capture/video_capture.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/peer.hpp"
#include "hela/eillance/rtc/stream.hpp"
#include "hela/eillance/rtc/stream_source.hpp"
#include "hela/eillance/rtc/transport.hpp"
#include "hela/eillance/utils/memory.h"


namespace hela::eillance {

class RtcProducer {
  using PeerTrack = std::shared_ptr<Peer::Track>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;
  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;

  class Client {
    friend class RtcProducer;

    std::string id_;
    PeerTrack track_;

  public:
    Client(std::string id, std::shared_ptr<Peer::Track> track)
        : id_(std::move(id)),
          track_(std::move(track)) {}
  };

public:
  explicit RtcProducer() {
    av_stream_ = std::make_shared<Stream>(
        std::make_shared<StreamSource>(Capture(std::make_unique<VideoCapture>())),
        std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

    // set callback responsible for sample sending
    av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      auto tracks = GetAllClientsInReadyState(type);
      if (!tracks.empty()) {
        SendMessageToAllTracks(sample, type, sampleTime, tracks);
      }
      DispatchQueue::MainThread().dispatch([this, ws]() {
        if (peers_.empty()) {
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
  virtual ~RtcProducer() = default;

  void AddPeer(const std::string id, const std::shared_ptr<Peer>& peer) {
    peers_.emplace(id, peer);
  }

  auto GetPeer(const std::string id) -> std::shared_ptr<Peer> {
    if (auto it = peers_.find(id); it != peers_.end()) {
      return peers_.at(id);
    }
    return nullptr;
  }

  void RemovePeer(const std::string& id) {
    peers_.erase(id);
  }

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) {
    av_stream_->Start();
  }

  static auto AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> {
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

  static auto AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> {
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

  void ConfigureVideo(VideoConfig conf) {
    // TODO
  }

private:
  std::vector<Client> GetAllClientsInReadyState(Stream::Source::Type type) {
    std::vector<Client> clients;
    // get track for given type
    auto get_track_data = [type](const std::shared_ptr<Peer>& peer) {
      return type == Stream::Source::Type::Video ? peer->video : peer->audio;
    };

    // get all peers with Ready state
    for (const auto& id_peer: peers_) {
      auto id = id_peer.first;
      auto peer = id_peer.second;
      auto opt_track_data = get_track_data(peer);
      if (peer->getState() == Peer::State::Ready && opt_track_data.has_value()) {
        clients.emplace_back(id, opt_track_data.value());
      }
    }
    return clients;
  }

  static void SendMessageToAllTracks(const rtc::binary& sample, Stream::Source::Type type, uint64_t sampleTime, const std::vector<Client>& clients) {
    const auto* stream_type = type == Stream::Source::Type::Video ? "video" : "audio";

    for (const auto& client: clients) {
      ProcessTrack(client, sampleTime, sample, stream_type);
    }
  }

  static void ProcessTrack(const Client& client, uint64_t sampleTime, const rtc::binary& message, const char* streamType) {
    auto peer = client.id_;
    auto track_data = client.track_;
    auto elapsed_seconds = static_cast<double>(sampleTime) / Time::uS;
    const auto rtp_config = track_data->sender->rtpConfig;
    auto elapsed_timestamp = rtp_config->secondsToTimestamp(elapsed_seconds);

    UpdateRtpConfig(elapsed_timestamp, track_data, rtp_config);

    LOGV << "Sending " << streamType << " sample with size: " << message.size() << " to " << peer;
    SendSample(message, streamType, track_data);
  }

  static void UpdateRtpConfig(uint32_t elapsedTimestamp, const PeerTrack& track, const std::shared_ptr<rtc::RtpPacketizationConfig>& rtp_config) {
    // set new timestamp
    rtp_config->timestamp = rtp_config->startTimestamp + elapsedTimestamp;

    // get elapsed time in clock rate from last RTCP sender report
    auto report_elapsed_timestamp = rtp_config->timestamp - track->sender->lastReportedTimestamp();
    // check if last report was at least 1 second ago
    if (rtp_config->timestampToSeconds(report_elapsed_timestamp) > 1) {
      track->sender->setNeedsToReport();
    }
  }

  static void SendSample(rtc::binary message, const char* streamType, const PeerTrack& track) {
    bool send = false;
    try {
      // send sample
      if (likely(!message.empty())) {
        send = track->track->send(message);
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
