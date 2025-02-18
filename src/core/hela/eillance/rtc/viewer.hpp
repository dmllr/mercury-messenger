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

struct Device {
  struct Camera {
    struct Resolution {
      std::uint16_t width;
      std::uint16_t height;
    };
    std::string id;
    std::string name;
    std::vector<Resolution> resolutions;
  };
  using Profile = std::vector<Camera>;
  std::string id;
  std::string name;
  Profile profile;
};

class RtcViewer final : public RtcTransport {
  using Json = nlohmann::json;
  using Devices = std::vector<Device>;
  using OnClientListChangedCallback = std::function<void(const Devices&)>;
  using OnProfileReceivedCallback = std::function<void(const std::string&, const Device::Profile&)>;
  using OnFrameReceivedCallback = std::function<void(const rtc::binary&)>;

  /// @brief Audio and video stream
  std::shared_ptr<Stream> av_stream_;

  /// @brief Connected publishers
  Devices devices_;

  /// @brief A callback to be called when a client list is updated
  std::shared_ptr<OnClientListChangedCallback> on_client_list_changed_{};
  /// @brief A callback to be called when a profile info is received
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  /// @brief A callback to be called when a new frame is received
  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};

public:
  RtcViewer() = default;
  RtcViewer(RtcViewer&&) = delete;
  RtcViewer(const RtcViewer&) = delete;
  RtcViewer& operator=(RtcViewer&&) = delete;
  RtcViewer& operator=(const RtcViewer&) = delete;
  ~RtcViewer() final = default;

  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final { }

  void SendStreamRequest(const std::string& client_id, const Config& config) {
    Json request = {
        {"to", client_id},
        {"type", "streamRequest"},
        {"config", {
          {"video", {
            {"cameraId", config.video_config.cameraId},
            {"width", config.video_config.width},
            {"height", config.video_config.height},
            {"frameRate", config.video_config.frameRate},
            {"bitrate", config.video_config.bitrate},
          },
          {"audio", {}},
       }},
    }};
    SendPeerMessage(request);
  }

  [[maybe_unused]] void SetClientChangedCallback(const OnClientListChangedCallback& callback) {
    on_client_list_changed_ = std::make_shared<OnClientListChangedCallback>(callback);
  }

  [[maybe_unused]] void SetProfileReceivedCallback(const OnProfileReceivedCallback& callback) {
    on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback);
  }

  [[maybe_unused]] void SetFrameReceivedCallback (const OnFrameReceivedCallback & callback) {
    on_frame_received_ = std::make_shared<OnFrameReceivedCallback >(callback);
  }

  void SetupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id,
                        const std::shared_ptr<rtc::DataChannel>& data_channel) {
    peer->data_channel_ = data_channel;
    peer->data_channel_->onOpen([]() {});
    peer->data_channel_->onClosed([]() {});
    peer->data_channel_->onMessage(nullptr, [this, &peer_id, peer](const std::string& msg) {
      if (msg.rfind("Ping", 0) == 0) {  // NOLINT(*-use-starts-ends-with) starts_with is not supported by all compilers
        auto ping_pong_interval = std::chrono::milliseconds(1000); // NOLINT(*-magic-numbers)
        DispatchQueue::MainThread().DispatchAfter(ping_pong_interval, [peer]() {
          if (peer->data_channel_->isOpen()) {
            peer->data_channel_->send("Pong");
          }
        });
        ReportPeerStateChange(peer_id, EventSource::ping, 1);
        return;
      }

      ProcessDataChannelMessage(peer_id, Json::parse(msg));
    });
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

    if (type == "offer") {
      auto sdp = message["sdp"].get<std::string>();
      std::shared_ptr<Peer> const peer = CreatePeer(peer_id);
      // NOLINTNEXTLINE(*-unnecessary-value-param)
      peer->peer_connection_->onDataChannel([this, peer_id, peer](std::shared_ptr<rtc::DataChannel> data_channel) {
        SetupDataChannel(peer, peer_id, data_channel);
      });
      peer->peer_connection_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        AddPeer(peer_id, peer);
      });
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void ProcessSystemMessage(const std::string& type, Json message) final {
    if (type == "info streamers") {
      UpdateClientList(message["clients"]);
    } else {
      throw std::runtime_error("Unknown message type in Viewer's ProcessSystemMessage: " + type);
    }
  }

  void ProcessDataChannelMessage(const std::string& peer_id, Json message) {
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();
    if (type == "info profile") {
      if (on_profile_received_ != nullptr) {
        auto&& profile = ParseDeviceProfile(message.at("profile"));
        (*on_profile_received_)(peer_id, profile);
      }
    } else if (type == "termination") {
      RemovePeer(peer_id);
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
    // create depacketizer
    auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>(rtc::NalUnit::Separator::StartSequence);
    // set handler
    track->setMediaHandler(depacketizer);
    track->onOpen(onOpen);
    track->onFrame(
        [this](const rtc::binary& data, rtc::FrameInfo fi) {
          if (on_frame_received_ != nullptr) {
            (*on_frame_received_)(data);
          }
        });

    return std::make_shared<Peer::Track>(track);
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

    return std::make_shared<Peer::Track>(track);
  }

  Json GetPublishMessage() final {
    return Json{
        {"kind", "viewer"},
    };
  }

  void UpdateClientList(const nlohmann::json& json) {
    // Clear existing clients
    devices_.clear();

    if (!json.is_array()) {
      LOGW << "Received invalid client list format";
      return;
    }

    // Parse new clients
    for (const auto& item: json) {
      Device client {
        .id = item.at("id").get<std::string>(),
        .name = item.at("name").get<std::string>(),
        .profile = ParseDeviceProfile(item.at("profile"))
      };
      devices_.push_back(std::move(client));
    }

    if (on_client_list_changed_ != nullptr) {
      (*on_client_list_changed_)(devices_);
    }

    // for debugging purposes
    LOGI << "Updated client list:";
    for (const auto& client: devices_) {
      LOGI << "  - " << client.id.c_str() << " (" << client.name.c_str() << ") with " << client.profile.size() << " camera entries";
    }
  }

  static auto ParseDeviceProfile(Json jprofile) -> std::vector<Device::Camera> {
    auto profile = std::vector<Device::Camera>{};
    for (const auto& cam_item : jprofile) {
      Device::Camera camera;
      camera.id = cam_item.at("id").get<std::string>();
      camera.name = cam_item.at("name").get<std::string>();

      const auto& resolutions_json = cam_item.at("resolutions");
      for (const auto& res_item : resolutions_json) {
        Device::Camera::Resolution res{};
        res.width = res_item.at("width").get<std::uint16_t>();
        res.height = res_item.at("height").get<std::uint16_t>();
        camera.resolutions.push_back(res);
      }
      profile.push_back(camera);
    }
    return profile;
  }

};

}  // namespace hela::eillance
