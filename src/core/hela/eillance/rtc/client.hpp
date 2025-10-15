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
#include "hela/eillance/crypto/crypto.hpp"


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

class RtcClient final : public RtcTransportDelegate {
  using Json = nlohmann::json;
  using PeerTrack = std::shared_ptr<Peer::Track>;
  using OnFrameReceivedCallback = std::function<void(const rtc::binary&)>;
  using OnProfileReceivedCallback = std::function<void(const std::string&, const Device::Profile&)>;
  using OnChatMessageReceivedCallback = std::function<void(const std::string&, const std::string&)>;

  std::shared_ptr<Stream> av_stream_;
  std::shared_ptr<RtcTransport> transport_;

  std::shared_ptr<OnFrameReceivedCallback> on_frame_received_{};
  std::shared_ptr<OnProfileReceivedCallback> on_profile_received_{};
  std::shared_ptr<OnChatMessageReceivedCallback> on_chat_message_received_{};

public:
  explicit RtcClient() {
    av_stream_ = std::make_shared<Stream>(
        std::make_shared<StreamSource>(Capture(std::make_unique<VideoCapture>())),
        std::make_shared<StreamSource>(Capture(std::make_unique<AudioCapture>())));

    transport_ = std::make_shared<RtcTransport>(this);

    av_stream_->OnSample([this, ws = make_weak_ptr(av_stream_)](Stream::Source::Type type, uint64_t sampleTime, const rtc::binary& sample) {
      SendSampleToPeers(sample, type, sampleTime);
    });
  }

  RtcClient(RtcClient&&) = delete;
  RtcClient(const RtcClient&) = delete;
  RtcClient& operator=(RtcClient&&) = delete;
  RtcClient& operator=(const RtcClient&) = delete;
  ~RtcClient() override = default;

  void SetId(const std::string& id) const { transport_->SetId(id); }
  void SetName(const std::string& name) const { transport_->SetName(name); }
  void Start() const { transport_->Start(); }
  void Terminate() const { transport_->Terminate(); }
  void SetStatusCallback(const std::function<void(const std::string& peer_id, EventSource, int)>& callback) const { transport_->SetStatusCallback(callback); }
  void SetFrameReceivedCallback(const OnFrameReceivedCallback& callback) { on_frame_received_ = std::make_shared<OnFrameReceivedCallback>(callback); }
  void SetProfileReceivedCallback(const OnProfileReceivedCallback& callback) { on_profile_received_ = std::make_shared<OnProfileReceivedCallback>(callback); }
  void SetChatMessageReceivedCallback(const OnChatMessageReceivedCallback& callback) { on_chat_message_received_ = std::make_shared<OnChatMessageReceivedCallback>(callback); }

  void Call(const std::string& peer_id) {
    const auto peer = transport_->CreatePeer(peer_id);
    peer->data_channel_ = CreateDataChannel(peer->peer_connection_, peer_id);
    transport_->AddPeer(peer_id, peer);
  }

  void Hangup(const std::string& peer_id) const {
    if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr) {
        Json message = {{"type", "termination"}};
        transport_->SendPeerDataChannelMessage(peer_id, message);
        peer->peer_connection_->close();
        transport_->RemovePeer(peer_id);
    }
  }

  void SendChatMessage(const std::string& peer_id, const std::string& message) const {
      if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr && peer->session_cipher_->IsReady()) {
          auto encrypted_payload = peer->session_cipher_->Encrypt(message);
          if (encrypted_payload.empty()) {
              LOGE << "Failed to encrypt chat message for peer " << peer_id;
              return;
          }
          Json json_message = {
              {"type", "chat"},
              {"payload", rtc::binary(encrypted_payload.begin(), encrypted_payload.end())}
          };
          transport_->SendPeerDataChannelMessage(peer_id, json_message);
      } else {
          LOGW << "Cannot send message: peer " << peer_id << " not ready or session is not secure.";
      }
  }

  void Configure(const Config& config) const {
    av_stream_->Video()->Configure(config);
    av_stream_->Audio()->Configure(config);
  }

  void OnPeerReady(const std::shared_ptr<Peer>&) override {
    av_stream_->Start();
  }

  void ProcessPeerMessage(Json message) override {
    auto from_it = message.find("from");
    if (from_it == message.end()) return;
    auto peer_id = from_it->get<std::string>();

    auto type_it = message.find("type");
    if (type_it == message.end()) return;
    auto type = type_it->get<std::string>();

    if (type == "offer") {
      auto sdp = message["sdp"].get<std::string>();
      const std::shared_ptr<Peer> peer = transport_->CreatePeer(peer_id);
      peer->peer_connection_->onDataChannel([this, peer_id, peer](const std::shared_ptr<rtc::DataChannel>& data_channel) {
        SetupDataChannel(peer, peer_id, data_channel);
      });
      peer->peer_connection_->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
      transport_->AddPeer(peer_id, peer);
    } else if (type == "answer") {
      if (const auto peer = transport_->GetPeer(peer_id); peer != nullptr) {
        auto sdp = message["sdp"].get<std::string>();
        auto answer = rtc::Description(sdp, type);
        peer->peer_connection_->setRemoteDescription(answer);
      }
    }
  }

  void ProcessSystemMessage(const std::string& type, Json /*message*/) override {
    LOGW << "Unhandled system message of type: " << type;
  }

  std::shared_ptr<Peer::Track> AddVideo(const std::shared_ptr<rtc::PeerConnection>& pc, const std::function<void(void)>& onOpen) override {
    auto video = rtc::Description::Video("video", rtc::Description::Direction::SendOnly);
    constexpr auto payload_type = 102;
    constexpr auto ssrc = 10001;
    video.addH264Codec(payload_type);
    video.addSSRC(ssrc, "video-stream", "stream1", "video-label");

    auto track = pc->addTrack(video);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "video-stream", payload_type, rtc::H264RtpPacketizer::ClockRate);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(rtc::NalUnit::Separator::StartSequence, rtpConfig);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

  std::shared_ptr<Peer::Track> AddAudio(const std::shared_ptr<rtc::PeerConnection>& pc, const std::function<void(void)>& onOpen) override {
    auto audio = rtc::Description::Audio("audio", rtc::Description::Direction::SendOnly);
    constexpr auto payload_type = 111;
    constexpr auto ssrc = 10002;
    audio.addOpusCodec(payload_type);
    audio.addSSRC(ssrc, "audio-stream", "stream1", "audio-label");
    auto track = pc->addTrack(audio);

    auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, "audio-stream", payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
    auto packetizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfig);
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
    packetizer->addToChain(srReporter);
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    packetizer->addToChain(nackResponder);
    track->setMediaHandler(packetizer);
    track->onOpen(onOpen);

    return std::make_shared<Peer::Track>(track, srReporter);
  }

  void OnTrack(const std::string&, std::shared_ptr<rtc::Track> track) override {
    if (track->mid().find("video") != std::string::npos) {
      auto depacketizer = std::make_shared<rtc::H264RtpDepacketizer>();
      track->setMediaHandler(depacketizer);
      track->onFrame([this](const rtc::binary& data, rtc::FrameInfo) {
        if (on_frame_received_) (*on_frame_received_)(data);
      });
    }
  }

private:
  void SendSampleToPeers(const rtc::binary& sample, Stream::Source::Type type, uint64_t sampleTime) const {
    for (const auto& [id, peer] : transport_->GetPeers()) {
      if (peer->GetState() == Peer::State::ready) {
        auto track = (type == Stream::Source::Type::video) ? peer->video_ : peer->audio_;
        if (track) {
          auto elapsedSeconds = static_cast<double>(sampleTime) / Time::uS;
          const auto rtpConfig = track->Reporter()->rtpConfig;
          auto elapsedTimestamp = rtpConfig->secondsToTimestamp(elapsedSeconds);
          rtpConfig->timestamp = rtpConfig->startTimestamp + elapsedTimestamp;

          auto reportElapsedTimestamp = rtpConfig->timestamp - track->Reporter()->lastReportedTimestamp();
          if (rtpConfig->timestampToSeconds(reportElapsedTimestamp) > 1) {
            track->Reporter()->setNeedsToReport();
          }

          if (!sample.empty()) {
            track->Send(sample);
          }
        }
      }
    }
  }

  void ProcessDataChannelMessage(const std::string& peer_id, Json message) const {
    const auto itr = message.find("type");
    if (itr == message.end()) {
      return;
    }
    auto type = itr->get<std::string>();
    const auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    };

    if (type == "profile") {
      if (on_profile_received_) {
        (*on_profile_received_)(peer_id, ParseDeviceProfile(message.at("profile")));
      }
    } else if (type == "termination") {
      transport_->RemovePeer(peer_id);
    } else if (type == "key_exchange") {
        auto pub_key_bytes = message.at("key").get<rtc::binary>();
        if (peer->session_cipher_->ComputeSharedSecret({pub_key_bytes.begin(), pub_key_bytes.end()})) {
            LOGI << "Successfully computed shared secret with peer " << peer_id;
            // If we are the callee, send back our key
            if (!message.value("is_reply", false)) {
                auto my_pub_key = peer->session_cipher_->GetPublicKey();
                Json reply = {
                    {"type", "key_exchange"},
                    {"key", rtc::binary(my_pub_key.begin(), my_pub_key.end())},
                    {"is_reply", true}
                };
                transport_->SendPeerDataChannelMessage(peer_id, reply);
            }
        } else {
            LOGE << "Failed to compute shared secret with peer " << peer_id;
        }
    } else if (type == "chat") {
        if (on_chat_message_received_ && peer->session_cipher_->IsReady()) {
            auto payload = message.at("payload").get<rtc::binary>();
            auto decrypted_message = peer->session_cipher_->Decrypt({payload.begin(), payload.end()});
            if (!decrypted_message.empty()) {
                (*on_chat_message_received_)(peer_id, decrypted_message);
            } else {
                LOGW << "Failed to decrypt message from " << peer_id;
            }
        }
    }
  }

  void SetupDataChannel(const std::shared_ptr<Peer>& peer, const std::string& peer_id, const std::shared_ptr<rtc::DataChannel>& data_channel) const {
    peer->data_channel_ = data_channel;
    data_channel->onOpen([this, peer_id, peer]() {
      // Send device profile
      const Json profile_message = {
          {"type", "profile"},
          {"profile", GetDeviceProfileAsJson()},
      };
      transport_->SendPeerDataChannelMessage(peer_id, profile_message);

      // Initiate key exchange
      auto my_pub_key = peer->session_cipher_->GetPublicKey();
      Json key_exchange_message = {
        {"type", "key_exchange"},
        {"key", rtc::binary(my_pub_key.begin(), my_pub_key.end())},
        {"is_reply", false}
      };
      transport_->SendPeerDataChannelMessage(peer_id, key_exchange_message);
    });
    data_channel->onMessage(nullptr, [this, peer_id](const std::string& msg) {
      try {
        ProcessDataChannelMessage(peer_id, Json::parse(msg));
      } catch (const Json::parse_error& e) {
        LOGE << "Failed to parse data channel message: " << e.what();
      }
    });
  }

  std::shared_ptr<rtc::DataChannel> CreateDataChannel(const std::shared_ptr<rtc::PeerConnection>& pc, const std::string& peer_id) const {
    auto data_channel = pc->createDataChannel("control");
    auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
        peer = std::make_shared<Peer>(pc);
    }
    SetupDataChannel(peer, peer_id, data_channel);
    return data_channel;
  }

  static Device::Profile ParseDeviceProfile(const Json& jprofile) {
    Device::Profile profile;
    for (const auto& cam_item : jprofile) {
      Device::Camera camera;
      camera.id = cam_item.at("id").get<std::string>();
      camera.name = cam_item.at("name").get<std::string>();
      for (const auto& res_item : cam_item.at("resolutions")) {
        camera.resolutions.push_back({res_item.at("width").get<uint16_t>(), res_item.at("height").get<uint16_t>()});
      }
      profile.push_back(camera);
    }
    return profile;
  }
};

}  // namespace hela::eillance
