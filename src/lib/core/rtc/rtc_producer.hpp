#pragma once

#include <memory>
#include <utility>

#include "logging/logging.h"
#include "utils/memory.h"

#include "nlohmann/json.hpp"
#include "rtc/rtc.h"

#include "core/rtc/peer.hpp"
#include "core/rtc/stream.hpp"
#include "core/stream/fileparser.hpp"
#include "core/stream/video_stream_source.hpp"

#include "rtc_transport_base.hpp"


class RtcProducer : public RtcTransportBase {
  using Json = nlohmann::json;
  using TrackData = std::shared_ptr<Peer::Track>;

  class ClientTrack {
    friend class RtcProducer;

    std::string id_;
    TrackData trackData_;

  public:
    ClientTrack(std::string id, std::shared_ptr<Peer::Track> trackData)
        : id_(std::move(id)),
          trackData_(std::move(trackData)) {}
  };

public:
  explicit RtcProducer() {
    av_stream = std::make_shared<Stream>(std::make_shared<VideoStreamSource>(), std::make_shared<OPUSFileParser>("silence", true));

    // set callback responsible for sample sending
    av_stream->onSample([this, ws = make_weak_ptr(av_stream)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      auto tracks = GetAllTracksInReadyState(type);
      if (!tracks.empty()) {
        SendMessageToAllTracks(sample, type, sampleTime, tracks);
      }
      StopStreamIfNoPeers(ws);
    });
  }
  RtcProducer(RtcProducer&&) = delete;
  RtcProducer(const RtcProducer&) = delete;
  RtcProducer& operator=(RtcProducer&&) = delete;
  RtcProducer& operator=(const RtcProducer&) = delete;
  virtual ~RtcProducer() = default;

  void StartServer() {
    Start("server");
  }


private:
  std::vector<ClientTrack> GetAllTracksInReadyState(Stream::Source::Type type) {
    std::vector<ClientTrack> tracks;
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
        tracks.emplace_back(id, opt_track_data.value());
      }
    }
    return tracks;
  }

  static void SendMessageToAllTracks(const rtc::binary& sample, Stream::Source::Type type, uint64_t sampleTime, const std::vector<ClientTrack>& tracks) {
    const auto* stream_type = type == Stream::Source::Type::Video ? "video" : "audio";

    for (const auto& client_track: tracks) {
      ProcessTrack(client_track, sampleTime, sample, stream_type);
    }
  }

  static void ProcessTrack(const ClientTrack& clientTrack, uint64_t sampleTime, const rtc::binary& message, const char* streamType) {
    auto peer = clientTrack.id_;
    auto track_data = clientTrack.trackData_;
    auto elapsed_seconds = static_cast<double>(sampleTime) / Units::Microseconds;
    const auto rtp_config = track_data->sender->rtpConfig;
    auto elapsed_timestamp = rtp_config->secondsToTimestamp(elapsed_seconds);

    UpdateRtpConfig(elapsed_timestamp, track_data, rtp_config);

    LOGV << "Sending " << streamType << " sample with size: " << message.size() << " to " << peer;
    SendSample(message, streamType, track_data);
  }

  static void UpdateRtpConfig(uint32_t elapsedTimestamp, const TrackData& trackData, const std::shared_ptr<rtc::RtpPacketizationConfig>& rtp_config) {
    // set new timestamp
    rtp_config->timestamp = rtp_config->startTimestamp + elapsedTimestamp;

    // get elapsed time in clock rate from last RTCP sender report
    auto report_elapsed_timestamp = rtp_config->timestamp - trackData->sender->lastReportedTimestamp();
    // check if last report was at least 1 second ago
    if (rtp_config->timestampToSeconds(report_elapsed_timestamp) > 1) {
      trackData->sender->setNeedsToReport();
    }
  }

  static void SendSample(rtc::binary message, const char* streamType, const TrackData& trackData) {
    bool send = false;
    try {
      // send sample
      if (likely(!message.empty())) {
        send = trackData->track->send(message);
      }
    } catch (const std::exception& e) {
      LOGE << "Exception. Unable to send " << streamType << " packet." << e.what();
      send = false;
    }
    if (!send) {
      LOGE << "Unable to send " << streamType << " packet";
    }
  }

  void StopStreamIfNoPeers(const std::weak_ptr<Stream>& ws) {
    MainThread.dispatch([this, ws]() {
      if (peers_.empty()) {
        // we have no peers, stop the stream
        if (auto stream = ws.lock()) {
          stream->stop();
        }
      }
    });
  }

protected:
  void wsOnMessage(Json message) final {
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
      std::shared_ptr<Peer> c = CreatePeerConnection(id);
      peers_.emplace(id, c);
    } else if (type == "answer") {
      if (auto jt = peers_.find(id); jt != peers_.end()) {
        auto pc = peers_.at(id)->peerConnection;
        auto sdp = message["sdp"].get<std::string>();
        auto answer = rtc::Description(sdp, type);
        pc->setRemoteDescription(answer);
      }
    } else if (type == "offer") {
      // do nothing
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void onPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
    av_stream->start();
  }

  auto CreatePeerConnection(const std::string& id) -> std::shared_ptr<Peer> {
    auto pc = CreateDefaultPeerConnection(id);

    std::shared_ptr<Peer> peer(new Peer(pc));

    peer->video = AddVideo(
        pc, rtc::Description::Direction::SendOnly,
        102, 10001, "video-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, true);
            }
          });
          LOGI << "Video from " << id << " opened";
        },
        nullptr);

    peer->audio = AddAudio(
        pc, rtc::Description::Direction::SendOnly,
        111, 10002, "audio-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              AddToStream(c, false);
            }
          });
          LOGI << "Audio from " << id << " opened";
        },
        nullptr);

    peer->dataChannel = CreateDefaultDataChannel(pc, id);

    pc->setLocalDescription();

    return peer;
  }

  static auto AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const rtc::Description::Direction direction,
      const uint8_t payload_type, const uint32_t ssrc, const std::string& cname, const std::string& msid,
      const std::function<void(void)>& onOpen,
      const std::function<void(rtc::binary message)>& onMessage) -> std::shared_ptr<Peer::Track> {
    auto video = rtc::Description::Video(cname, direction);
    video.addH264Codec(payload_type);
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);
    // create RTP configuration
    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payload_type, rtc::H264RtpPacketizer::defaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtp_config);
    // add RTCP SR handler
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(srReporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

  static auto AddAudio(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const rtc::Description::Direction direction,
      const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string& msid,
      const std::function<void(void)>& onOpen,
      const std::function<void(rtc::binary message)>& onMessage) -> std::shared_ptr<Peer::Track> {
    auto audio = rtc::Description::Audio(cname, direction);
    audio.addOpusCodec(payloadType);
    audio.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(audio);
    // create RTP configuration
    auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtp_config);
    // add RTCP SR handler
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(srReporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

private:
  /// Audio and video stream
  std::shared_ptr<Stream> av_stream;
};
