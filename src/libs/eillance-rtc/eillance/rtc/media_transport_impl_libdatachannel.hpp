#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/rtc/connection_state_conversion.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/protocol.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::rtc {

class RtcMediaTransport : public transport::MediaTransport::Interface {
 public:
  RtcMediaTransport() {
    rtcConfig_.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
  }

  void SetIceServers(std::string const& json_config) override {
    try {
      auto json = nlohmann::json::parse(json_config);
      if (!json.is_array()) return;

      ::rtc::Configuration new_config;
      for (auto const& entry : json) {
        if (!entry.contains("urls")) continue;
        auto urls_entry = entry["urls"];
        std::string url;
        if (urls_entry.is_array() && !urls_entry.empty())
          url = urls_entry[0].get<std::string>();
        else if (urls_entry.is_string())
          url = urls_entry.get<std::string>();
        if (url.empty()) continue;

        // Basic STUN/TURN handling same as DataTransport
        new_config.iceServers.emplace_back(url);
      }
      if (!new_config.iceServers.empty()) {
        std::lock_guard<std::mutex> const lock(configMutex_);
        rtcConfig_ = new_config;
      }
    } catch (...) {
    }
  }

  void Connect(ContactID const& peer_id) override {
    ::rtc::Configuration config;
    {
      std::lock_guard<std::mutex> const lock(configMutex_);
      config = rtcConfig_;
    }
    auto pc = std::make_shared<::rtc::PeerConnection>(config);

    pc->onStateChange([this, peer_id](::rtc::PeerConnection::State const state) -> void {
      EventQueue::Instance().Push(EventMediaPeerConnectionStateChanged{peer_id, transport::ToEillanceState(state)});
    });

    pc->onGatheringStateChange([this, peer_id](::rtc::PeerConnection::GatheringState const state) -> void {
      EventQueue::Instance().Push(EventMediaGatheringStateChanged{peer_id, static_cast<int>(state)});
    });

    std::lock_guard<std::mutex> const lock(pcsMutex_);
    pcs_[peer_id] = pc;
  }

  void Disconnect(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      it->second->close();
      pcs_.erase(it);
    }
  }

  void CreateOffer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      it->second->setLocalDescription();
    }
  }

  void CreateAnswer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      it->second->setLocalDescription(::rtc::Description::Type::Answer);
    }
  }

  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      it->second->setRemoteDescription(::rtc::Description(sdp, type));
    }
  }

  void StartLocalMedia(bool /*is_video*/) override {
    // No-op for CLI / Desktop DataChannel currently.
    // Media capabilities will be orchestrated differently for native clients.
  }

  void StopLocalMedia() override {
    // No-op
  }

  void SetLocalMediaEnabled(bool /*audio*/, bool /*video*/) override {
    // No-op
  }

  auto AddVideo(ContactID const& peer_id) -> std::shared_ptr<transport::ITrack> override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    auto const it = pcs_.find(peer_id);
    if (it == pcs_.end()) {
      return nullptr;
    }
    auto const pc = it->second;

    auto video = ::rtc::Description::Video("video", ::rtc::Description::Direction::SendRecv);
    video.addH264Codec(102);
    video.addSSRC(10001, "video-stream", "stream1", "video-label");
    auto track = pc->addTrack(video);
    auto rtp_config = std::make_shared<::rtc::RtpPacketizationConfig>(
        10001,
        "video-stream",
        102,
        ::rtc::H264RtpPacketizer::ClockRate
    );
    auto const packetizer =
        std::make_shared<::rtc::H264RtpPacketizer>(::rtc::NalUnit::Separator::StartSequence, rtp_config);
    auto reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(reporter);
    track->setMediaHandler(packetizer);
    return std::make_shared<RtcTrack>(track, reporter, rtp_config);
  }

  auto AddAudio(ContactID const& peer_id) -> std::shared_ptr<transport::ITrack> override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    auto const it = pcs_.find(peer_id);
    if (it == pcs_.end()) {
      return nullptr;
    }
    auto const pc = it->second;

    auto audio = ::rtc::Description::Audio("audio", ::rtc::Description::Direction::SendRecv);
    audio.addOpusCodec(111);
    audio.addSSRC(10002, "audio-stream", "stream1", "audio-label");
    auto track = pc->addTrack(audio);
    auto rtp_config = std::make_shared<::rtc::RtpPacketizationConfig>(
        10002,
        "audio-stream",
        111,
        ::rtc::OpusRtpPacketizer::DefaultClockRate
    );
    auto const packetizer = std::make_shared<::rtc::OpusRtpPacketizer>(rtp_config);
    auto reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(reporter);
    track->setMediaHandler(packetizer);
    return std::make_shared<RtcTrack>(track, reporter, rtp_config);
  }

  void AttachToPeer(ContactID const& peer_id, std::shared_ptr<transport::Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      peer->media_peer_connection = it->second;
    }
  }

 private:
  ::rtc::Configuration rtcConfig_;
  std::mutex configMutex_;

  std::unordered_map<ContactID, std::shared_ptr<::rtc::PeerConnection>> pcs_;
  mutable std::mutex pcsMutex_;
};

}  // namespace eillance::rtc
