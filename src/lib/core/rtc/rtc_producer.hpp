#pragma once

#include <memory>

#include "logging/logging.h"
#include "utils/memory.h"

#include "nlohmann/json.hpp"
#include "rtc/rtc.h"

#include "core/rtc/peer.hpp"
#include "core/rtc/stream.hpp"
#include "core/stream/video_stream_source.hpp"
#include "core/stream/fileparser.hpp"

#include "rtc_transport_base.hpp"


class RtcProducer : public RtcTransportBase {
  using json = nlohmann::json;

  struct ClientTrack {
    std::string id;
    std::shared_ptr<Peer::Track> trackData;

    ClientTrack(const std::string& id, std::shared_ptr<Peer::Track> trackData) {
      this->id = id;
      this->trackData = std::move(trackData);
    }
  };

public:
  explicit RtcProducer() {
    av_stream = std::make_shared<Stream>(std::make_shared<VideoStreamSource>(), std::make_shared<OPUSFileParser>("silence", true));

    // set callback responsible for sample sending
    av_stream->onSample([this, ws = make_weak_ptr(av_stream)](Stream::Source::Type type, uint64_t sampleTime, rtc::binary sample) {
      auto tracks{std::vector<ClientTrack>()};
      const auto* streamType = type == Stream::Source::Type::Video ? "video" : "audio";
      // get track for given type
      auto getTrackData =
          [type](const std::shared_ptr<Peer>& peer) {
            return type == Stream::Source::Type::Video ? peer->video : peer->audio;
          };

      // get all peers with Ready state
      for (const auto& id_peer: peers) {
        auto id = id_peer.first;
        auto peer = id_peer.second;
        auto optTrackData = getTrackData(peer);
        if (peer->getState() == Peer::State::Ready && optTrackData.has_value()) {
          auto trackData = optTrackData.value();
          tracks.emplace_back(id, trackData);
        }
      }

      if (!tracks.empty()) {
        auto message = rtc::make_message(std::move(sample));
        for (const auto& clientTrack: tracks) {
          auto peer = clientTrack.id;
          auto trackData = clientTrack.trackData;
          // sample time is in us, we need to convert it to seconds
          auto elapsedSeconds = double(sampleTime) / (1000 * 1000);
          auto rtpConfig = trackData->sender->rtpConfig;
          // get elapsed time in clock rate
          uint32_t elapsedTimestamp = rtpConfig->secondsToTimestamp(elapsedSeconds);

          // set new timestamp
          rtpConfig->timestamp = rtpConfig->startTimestamp + elapsedTimestamp;

          // get elapsed time in clock rate from last RTCP sender report
          auto reportElapsedTimestamp = rtpConfig->timestamp - trackData->sender->lastReportedTimestamp();
          // check if last report was at least 1 second ago
          if (rtpConfig->timestampToSeconds(reportElapsedTimestamp) > 1) {
            trackData->sender->setNeedsToReport();
          }

          LOGV << "Sending " << streamType << " sample with size: " << message->size() << " to " << peer;
          bool send = false;
          try {
            // send sample
            if (likely(!message->empty())) {
              send = trackData->track->send(*message);
            }
          } catch (const std::exception &e) {
            LOGE << "Exception. Unable to send " << streamType << " packet." << e.what();
            send = false;
          }
          if (!send) {
            LOGE << "Unable to send " << streamType << " packet";
            break;
          }
        }
      }

      MainThread.dispatch([this, ws]() {
        if (peers.empty()) {
          // we have no peers, stop the stream
          if (auto stream = ws.lock()) {
            stream->stop();
          }
        }
      });
    });
  }

  void startServer() {
    start("server");
  }


protected:
  void wsOnMessage(json message) final {
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
      std::shared_ptr<Peer> c = createPeerConnection(id);
      peers.emplace(id, c);
    } else if (type == "answer") {
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

  void onPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
    av_stream->start();
  }

  auto createPeerConnection(const std::string& id) -> std::shared_ptr<Peer> {
    auto pc = createDefaultPeerConnection(id);

    std::shared_ptr<Peer> peer(new Peer(pc));

    peer->video = addVideo(
        pc, rtc::Description::Direction::SendOnly,
        102, 10001, "video-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              addToStream(c, true);
            }
          });
          LOGI << "Video from " << id << " opened";
        },
        nullptr);

    peer->audio = addAudio(
        pc, rtc::Description::Direction::SendOnly,
        111, 10002, "audio-stream", "cam-stream-1",
        [this, id, wc = make_weak_ptr(peer)]() {
          MainThread.dispatch([this, wc]() {
            if (auto c = wc.lock()) {
              addToStream(c, false);
            }
          });
          LOGI << "Audio from " << id << " opened";
        },
        nullptr);

    peer->dataChannel = createDefaultDataChannel(pc, id);

    pc->setLocalDescription();

    return peer;
  }

  static auto addVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const rtc::Description::Direction direction,
      const uint8_t payloadType, const uint32_t ssrc, const std::string& cname, const std::string& msid,
      const std::function<void(void)>& onOpen,
      const std::function<void(rtc::binary message)>& onMessage) -> std::shared_ptr<Peer::Track> {
    auto video = rtc::Description::Video(cname, direction);
    video.addH264Codec(payloadType);
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);
    // create RTP configuration
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, rtc::H264RtpPacketizer::defaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
    // add RTCP SR handler
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    // add RTCP NACK handler
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

  static auto addAudio(
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
    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, payloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
    // create packetizer
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
    // add RTCP SR handler
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    // add RTCP NACK handler
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);
    // set handler
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

private:
  /// Audio and video stream
  std::shared_ptr<Stream> av_stream;
};
