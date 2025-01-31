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

class RtcConsumer : public RtcTransport {
  using Json = nlohmann::json;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;

public:
  RtcConsumer() = default;
  RtcConsumer(RtcConsumer&&) = delete;
  RtcConsumer(const RtcConsumer&) = delete;
  RtcConsumer& operator=(RtcConsumer&&) = delete;
  RtcConsumer& operator=(const RtcConsumer&) = delete;
  virtual ~RtcConsumer() = default;

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
    SendStreamRequest();
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

    if (type == "answer") {
      // shouldn't
    } else if (type == "offer") {
      auto sdp = message["sdp"].get<std::string>();
      std::shared_ptr<Peer> const peer = CreatePeerConnection(peer_id);
      auto pc = peer->PeerConnection();
      pc->setRemoteDescription(sdp);
      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        AddPeer(peer_id, peer);
      });
    } else if (type == "info profile") {
      // TODO
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void ProcessSystemMessage(const std::string& type, Json message) final {
    if (type == "info producers") {
      UpdateClientList(message["clients"]);
    } else {
      throw std::runtime_error("Unknown message type in Consumer's ProcessSystemMessage: " + type);
    }
  }

  auto AddVideo(
      const std::shared_ptr<rtc::PeerConnection>& pc,
      const std::function<void(void)>& onOpen) -> std::shared_ptr<Peer::Track> final {
    constexpr auto payload_type = 102;
    constexpr auto ssrc = 10001;
    constexpr auto msid = "cam-NAME-stream";
    constexpr auto cname = "video-stream";
    auto video = rtc::Description::Video(cname, rtc::Description::Direction::RecvOnly);
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
    auto audio = rtc::Description::Audio(cname, rtc::Description::Direction::RecvOnly);
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
        {"kind", "consumer"},
    };
  }

  void Configure(Config config) final {
  }

  void UpdateClientList(const Json& clients) {
    // TODO
  }

  void SendStreamRequest() {
    //    websocket.send(JSON.stringify(
    //        {
    //          to: selectedClientId,
    //          token: token,
    //          type: "streamRequest"
    //        }));
  }

};

}  // namespace hela::eillance
