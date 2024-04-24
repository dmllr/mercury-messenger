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

#include "rtc_transport.hpp"


class RtcProducer {
  using Json = nlohmann::json;
  using PeerTrack = std::shared_ptr<Peer::Track>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;
  RtcTransport transport_;
  DispatchQueue MainThread_ = DispatchQueue("Main", 1);

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
    av_stream_ = std::make_shared<Stream>(std::make_shared<VideoStreamSource>(), std::make_shared<OPUSFileParser>("silence", true));

    // set callback responsible for sample sending
    av_stream_->onSample([this, ws = make_weak_ptr(av_stream_)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      auto tracks = GetAllClientsInReadyState(type);
      if (!tracks.empty()) {
        SendMessageToAllTracks(sample, type, sampleTime, tracks);
      }
      StopStreamIfNoPeers(ws);
    });

    transport_.SetOnWSMessageCallback([this](Json message) { OnWSMessage(std::move(message)); });
    transport_.SetOnPeerReadyCallback([this](const std::shared_ptr<Peer>& peer) { OnPeerReady(std::move(peer)); });
  }
  RtcProducer(RtcProducer&&) = delete;
  RtcProducer(const RtcProducer&) = delete;
  RtcProducer& operator=(RtcProducer&&) = delete;
  RtcProducer& operator=(const RtcProducer&) = delete;
  virtual ~RtcProducer() = default;

  void StartServer() {
    transport_.Start("server");
  }

  [[maybe_unused]] void SetStatusCallback(const std::function<void(EventSource, int)>& callback) {
    transport_.SetStatusCallback(std::move(callback));
  }

private:
  std::vector<Client> GetAllClientsInReadyState(Stream::Source::Type type) {
    std::vector<Client> tracks;
    // get track for given type
    auto get_track_data = [type](const std::shared_ptr<Peer>& peer) {
      return type == Stream::Source::Type::Video ? peer->video : peer->audio;
    };

    // get all peers with Ready state
    for (const auto& id_peer: transport_.Peers()) {
      auto id = id_peer.first;
      auto peer = id_peer.second;
      auto opt_track_data = get_track_data(peer);
      if (peer->getState() == Peer::State::Ready && opt_track_data.has_value()) {
        tracks.emplace_back(id, opt_track_data.value());
      }
    }
    return tracks;
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
    auto elapsed_seconds = static_cast<double>(sampleTime) / Units::Microseconds;
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

  void StopStreamIfNoPeers(const std::weak_ptr<Stream>& ws) {
    MainThread_.dispatch([this, ws]() {
      if (transport_.Peers().empty()) {
        // we have no peers, stop the stream
        if (auto stream = ws.lock()) {
          stream->stop();
        }
      }
    });
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
      std::shared_ptr<Peer> c = CreatePeerConnection(id);
      transport_.Peers().emplace(id, c);
    } else if (type == "answer") {
      const auto peers = transport_.Peers();
      if (auto jt = peers.find(id); jt != peers.end()) {
        auto pc = peers.at(id)->peerConnection;
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

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) {
    av_stream_->start();
  }

  auto CreatePeerConnection(const std::string& id) -> std::shared_ptr<Peer> {
    auto pc = transport_.CreateDefaultPeerConnection(id);

    std::shared_ptr<Peer> peer(new Peer(pc));

    peer->video = AddVideo(
        pc, rtc::Description::Direction::SendOnly,
        102, 10001, "video-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread_.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              transport_.AddToStream(c, true);
            }
          });
          LOGI << "Video from " << id << " opened";
        },
        nullptr);

    peer->audio = AddAudio(
        pc, rtc::Description::Direction::SendOnly,
        111, 10002, "audio-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread_.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              transport_.AddToStream(c, false);
            }
          });
          LOGI << "Audio from " << id << " opened";
        },
        nullptr);

    peer->dataChannel = transport_.CreateDefaultDataChannel(pc, id);

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

};
