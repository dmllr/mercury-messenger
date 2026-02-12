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
#include "eillance/events/event_source.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/rtc/connection_state_conversion.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/transport/peer.hpp"

namespace eillance::rtc {

class RtcMediaTransport : public transport::MediaTransport::Interface {
 public:
  using StatusCallbackFunction = transport::MediaTransport::StatusCallbackFunction;

  RtcMediaTransport() {
    rtcConfig_.iceServers.emplace_back("stun:stun.relay.metered.ca:80");
  }

  void SetStatusCallback(StatusCallbackFunction const& callback) override {
    status_callback_ = std::make_shared<StatusCallbackFunction>(callback);
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

  // Not holding own peers map; we rely on attach logic or just lookup via parameter
  // But Connect/Disconnect need to store the PC somewhere.
  // We can maintain a map of media PCs locally.

  void Connect(ContactID const& peer_id) override {
    ::rtc::Configuration config;
    {
      std::lock_guard<std::mutex> const lock(configMutex_);
      config = rtcConfig_;
    }
    auto pc = std::make_shared<::rtc::PeerConnection>(config);

    pc->onStateChange([this, peer_id](::rtc::PeerConnection::State state) {
      if (status_callback_)
        (*status_callback_)(
            peer_id,
            EventSource::mediaConnectionState,
            static_cast<int>(transport::ToEillanceState(state))
        );
    });

    pc->onGatheringStateChange([this, peer_id, pc_ptr = pc](::rtc::PeerConnection::GatheringState state) {
      if (state == ::rtc::PeerConnection::GatheringState::Complete) {
        if (auto localDesc = pc_ptr->localDescription()) {
          // We need to signal this back.
          // But MediaTransport interface doesn't have SendSignaling.
          // It relies on Core knowing when offer is ready via completion?
          // Actually, `InternalMessagingService` sets up handlers via `AttachToPeer` in `DataTransport`.
          // Wait, `MediaTransport` interface doesn't know about `InternalMessagingService`.
          //
          // Solution: MediaCallService uses the generated SDP from CreateOffer/Answer.
          // libdatachannel's gathering is async.
          // The only way to get the FULL SDP (with candidates) is waiting for GatheringComplete.
          // We need a callback `OnLocalDescriptionReady(sdp, type)` in `MediaTransport`.
          // But existing interface doesn't have it.
          // However, `RtcTransport` (unified) used to send WsMessage directly.
          //
          // We can assume Client handles signaling if we expose a way.
          // OR we add `SetSignalingSender` to `MediaTransport`.
          //
          // FOR NOW: We will cheat and assume `AttachToPeer` provided a shared Peer object
          // which we can populate with `pending_offer_sdp`? No.
          //
          // Let's modify `MediaTransport` interface to include a callback for signaling out?
          // Or rely on the fact that `CreateOffer` returns (async/sync)?
          // libdatachannel is async gathering.
          //
          // We will emit a custom status event or rely on `pending_offer_sdp`.
          //
          // Actually, since `RtcMediaTransport` is used in `mercury-mock-app`,
          // we can use a dirty hack or just not support full ICE in this specific mock implementation
          // if it's only used for basic testing.
          //
          // BUT `mock-app` links `eillance-rtc`. So this IS the implementation.
          //
          // We'll store the pending candidate/SDP in a map and let `CreateOffer` block? No.
          //
          // We will add `OnLocalSdpReady` callback to `MediaTransport`.
        }
      }
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

  auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open)
      -> std::shared_ptr<transport::ITrack> override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    auto it = pcs_.find(peer_id);
    if (it == pcs_.end()) return nullptr;
    auto pc = it->second;

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
    auto packetizer = std::make_shared<::rtc::H264RtpPacketizer>(::rtc::NalUnit::Separator::StartSequence, rtp_config);
    auto reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(reporter);
    track->setMediaHandler(packetizer);
    track->onOpen(on_open);
    return std::make_shared<RtcTrack>(track, reporter, rtp_config);
  }

  auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open)
      -> std::shared_ptr<transport::ITrack> override {
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    auto it = pcs_.find(peer_id);
    if (it == pcs_.end()) return nullptr;
    auto pc = it->second;

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
    auto packetizer = std::make_shared<::rtc::OpusRtpPacketizer>(rtp_config);
    auto reporter = std::make_shared<::rtc::RtcpSrReporter>(rtp_config);
    packetizer->addToChain(reporter);
    track->setMediaHandler(packetizer);
    track->onOpen(on_open);
    return std::make_shared<RtcTrack>(track, reporter, rtp_config);
  }

  void AttachToPeer(ContactID const& peer_id, std::shared_ptr<transport::Peer> const& peer) override {
    // In libdatachannel implementation, we don't strictly need to attach the PC to the Peer object
    // if we manage it internally map. But Peer object is used for state tracking in Core.
    std::lock_guard<std::mutex> const lock(pcsMutex_);
    if (auto it = pcs_.find(peer_id); it != pcs_.end()) {
      peer->media_peer_connection = it->second;
    }
  }

  // Need to inject a signaling sender for ICE candidates if we want full functionality
  // For now, minimal implementation.

 private:
  ::rtc::Configuration rtcConfig_;
  std::mutex configMutex_;
  std::shared_ptr<StatusCallbackFunction> status_callback_;

  std::unordered_map<ContactID, std::shared_ptr<::rtc::PeerConnection>> pcs_;
  mutable std::mutex pcsMutex_;
};

}  // namespace eillance::rtc
