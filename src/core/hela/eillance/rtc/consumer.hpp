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

struct ClientInfo {
  std::string id;
  std::string name;
  nlohmann::json profile;
};

class RtcConsumer final : public RtcTransport {
  using Json = nlohmann::json;
  using OnClientListChangedCallback = std::function<void()>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;

  /// @brief Connected publishers
  std::vector<ClientInfo> clients_;

  /// @brief
  std::shared_ptr<OnClientListChangedCallback> on_client_list_changed_{};

public:
  RtcConsumer() = default;
  RtcConsumer(RtcConsumer&&) = delete;
  RtcConsumer(const RtcConsumer&) = delete;
  RtcConsumer& operator=(RtcConsumer&&) = delete;
  RtcConsumer& operator=(const RtcConsumer&) = delete;
  ~RtcConsumer() final = default;

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final {
  }

  void SendStreamRequest() {
    if (clients_.empty()) {
      return;
    }
    auto selected_client_id = clients_.begin()->id;
    Json request = {
        {"to", selected_client_id},
        {"type", "streamRequest"}
    };
    SendPeerMessage(request);
  }

  [[maybe_unused]] void SetClientAddedCallback(const OnClientListChangedCallback& callback) {
    on_client_list_changed_ = std::make_shared<OnClientListChangedCallback>(callback);
  }

private:
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
      pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        AddPeer(peer_id, peer);
      });

//      SendAnswer(peer_id, pc->localDescription().value());
//      pc->createAnswer([this, peer_id, pc](rtc::Description answer) {
//        pc->setLocalDescription(answer, [this, peer_id, pc]() {
//          if (pc->iceGatheringState() == rtc::PeerConnection::IceGatheringState::Complete) {
//            SendAnswer(peer_id, pc->localDescription().value());
//          } else {
//            pc->onGatheringStateChange([this, peer_id, pc](rtc::PeerConnection::IceGatheringState state) {
//              if (state == rtc::PeerConnection::IceGatheringState::Complete) {
//                SendAnswer(peer_id, pc->localDescription().value());
//              }
//            });
//          }
//        });
//      });

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
//    // create depacketizer
    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
    // add RTCP SR handler
    auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    depacketizer->addToChain(sr_reporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    depacketizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(depacketizer);
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
    // create depacketizer
    auto depacketizer = std::make_shared<rtc::OpusRtpDepacketizer>();
    // add RTCP SR handler
    auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(rtp_config);
    depacketizer->addToChain(sr_reporter);
    // add RTCP NACK handler
    auto nack_responder = std::make_shared<rtc::RtcpNackResponder>();
    depacketizer->addToChain(nack_responder);
    // set handler
    track->setMediaHandler(depacketizer);
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

  void UpdateClientList(const nlohmann::json& json) {
    // Clear existing clients
    clients_.clear();

    if (!json.is_array()) {
      LOGW << "Received invalid client list format";
      return;
    }

    // Parse new clients
    for (const auto& item : json) {
      ClientInfo client;
      client.id = item.at("id").get<std::string>();
      client.name = item.at("name").get<std::string>();
      client.profile = item.at("profile");

      // Validate profile format
      if (!client.profile.is_array()) {


        LOGW << "Invalid profile format for client " << client.id.c_str();
        continue;
      }

      clients_.push_back(std::move(client));
    }

    if (on_client_list_changed_ != nullptr) {
      (*on_client_list_changed_)();
    }

//    // Auto-select first client
//    if (!clients_.empty()) {
//      selectedClientId_ = clients_[0].id;
//      profile_ = clients_[0].profile;
//      HELA_LOG_DEBUG("Selected default client: %s", selectedClientId_.c_str());
//    } else {
//      selectedClientId_.clear();
//      profile_ = nlohmann::json::array();
//    }

    // for debugging purposes
    LOGI << "Updated client list:";
    for (const auto& client : clients_) {
      LOGI << "  - " << client.id.c_str() << " (" << client.name.c_str() << ") with " << client.profile.size() << " profile entries";
    }
  }

  void SendAnswer(const std::string& peer_id, const std::string& sdp) {
    Json answer = {
        {"to", peer_id},
        {"type", "answer"},
        {"sdp", sdp}
    };
    SendPeerMessage(answer);
  }
};

}  // namespace hela::eillance
