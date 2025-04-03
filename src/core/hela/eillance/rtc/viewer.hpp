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

/**
 * @brief RTC viewer implementation
 *
 * Handles viewing remote audio and video streams over WebRTC
 */
class RtcViewer final : public RtcTransportDelegate {
  using Json = nlohmann::json;
  using Devices = std::vector<Device>;
  using OnPeerListChangedCallback = std::function<void(const Devices&)>;
  using OnProfileReceivedCallback = std::function<void(const std::string&, const Device::Profile&)>;
  using OnFrameReceivedCallback = std::function<void(const rtc::binary&)>;

  /// @brief RTC transport delegate
  std::shared_ptr<RtcTransport> transport_;

  /// @brief Connected publishers
  Devices devices_;

  /// @brief A callback to be called when a peer list is updated
  std::shared_ptr<OnPeerListChangedCallback> on_peer_list_changed_{};
  /// @brief A callback to be called when a profile info is received
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  /// @brief A callback to be called when a new frame is received
  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};

public:
  RtcViewer() {
    transport_ = std::make_shared<RtcTransport>(this);
  }

  RtcViewer(RtcViewer&&) = delete;
  RtcViewer(const RtcViewer&) = delete;
  RtcViewer& operator=(RtcViewer&&) = delete;
  RtcViewer& operator=(const RtcViewer&) = delete;
  ~RtcViewer() = default;

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

  void SetStatusCallback(const std::function<void(const std::string& peer_id, EventSource, int)>& callback) {
    transport_->SetStatusCallback(callback);
  }

  // RtcTransportDelegate implementation
  void OnPeerReady(const std::shared_ptr<Peer>& /*peer*/) final { }

  Json GetPublishMessage() final {
    return Json{
        {"kind", "viewer"},
    };
  }

  Json ComposeConfig(const Config& config) const {
    return {
      {"video", {
        {"cameraId", config.video_config.cameraId},
        {"width", config.video_config.width},
        {"height", config.video_config.height},
        {"frameRate", config.video_config.frameRate},
        {"bitrate", config.video_config.bitrate},
      }},
      {"audio", {}},
    };
  }

  void SendStreamRequest(const std::string& peer_id, const Config& config) {
    Json request = {
        {"to", peer_id},
        {"type", "stream_request"},
        {"config", ComposeConfig(config)},
    };
    transport_->SendPeerMessage(peer_id, request);
  }

  void SendConfigurationRequest(const std::string& peer_id, const Config& config) {
    const Json request = {
        {"type", "configuration"},
        {"config", ComposeConfig(config)},
    };
    transport_->SendPeerDataChannelMessage(peer_id, request);
  }

  void RequestPeerList() {
    if (auto state = transport_->GetServiceConnectionState();
        state != ServiceConnectionState::online) {
      LOGE << "Cannot request peer list, not connected or not authenticated";
      return;
    }

    // Send a get_streamers request to the signaling server
    Json message = {
        {"type", "get_streamers"},
        {"token", ""}  // Token will be added by the transport
    };

    transport_->SendSystemMessage(message);
    LOGD << "Requested peer list update";
  }

  [[maybe_unused]] void SetPeerChangedCallback(const OnPeerListChangedCallback& callback) {
    on_peer_list_changed_ = std::make_shared<OnPeerListChangedCallback>(callback);
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
        transport_->ReportPeerStateChange(peer_id, EventSource::ping, 1);
        return;
      }

      ProcessDataChannelMessage(peer_id, Json::parse(msg));
    });
  }

  // RtcTransportDelegate implementation
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
      std::shared_ptr<Peer> const peer = transport_->CreatePeer(peer_id);
      // NOLINTNEXTLINE(*-unnecessary-value-param)
      peer->peer_connection_->onDataChannel([this, peer_id, peer](std::shared_ptr<rtc::DataChannel> data_channel) {
        SetupDataChannel(peer, peer_id, data_channel);
      });
      peer->peer_connection_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
      DispatchQueue::MainThread().Dispatch([this, peer_id, peer]() {
        transport_->AddPeer(peer_id, peer);
      });
    } else {
      throw std::runtime_error("Unknown message type " + type);
    }
  }

  void ProcessSystemMessage(const std::string& type, Json message) final {
    if (type == "streamers") {
      UpdatePeerList(message["peers"]);
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
    if (type == "profile") {
      if (on_profile_received_ != nullptr) {
        auto&& profile = ParseDeviceProfile(message.at("profile"));
        (*on_profile_received_)(peer_id, profile);
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

private:
  void UpdatePeerList(const nlohmann::json& json) {
    // Clear existing peers
    devices_.clear();

    if (json.is_array()) {
      // Parse new peers
      for (const auto &item: json) {
        Device peer{
            .id = item.at("id").get<std::string>(),
            .name = item.at("name").get<std::string>(),
            .profile = ParseDeviceProfile(item.at("profile"))
        };
        devices_.push_back(std::move(peer));
      }
    }

    if (on_peer_list_changed_ != nullptr) {
      (*on_peer_list_changed_)(devices_);
    }

    // for debugging purposes
    LOGI << "Updated peer list:";
    for (const auto& peer: devices_) {
      LOGI << "  - " << peer.id.c_str() << " (" << peer.name.c_str() << ") with " << peer.profile.size() << " camera entries";
    }
  }

  static auto ParseDeviceProfile(Json jprofile) -> std::vector<Device::Camera> {
    Device::Profile profile;
    profile.reserve(jprofile.size());
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
