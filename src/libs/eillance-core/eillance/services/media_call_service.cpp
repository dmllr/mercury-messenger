#include "eillance/services/media_call_service.hpp"

#include <chrono>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

MediaCallService::MediaCallService(std::shared_ptr<transport::Transport> transport)
    : transport_(std::move(transport)) {}

void MediaCallService::SetMessagingService(MessagingService* messaging_service) {
  messaging_service_ = messaging_service;
}

void MediaCallService::InitializeAvStream() {
  if (!video_capture_ || !audio_capture_) {
    return;
  }

  av_stream_ = std::make_shared<Stream>(
      std::make_shared<StreamSource>(std::move(*video_capture_)),
      std::make_shared<StreamSource>(std::move(*audio_capture_))
  );

  av_stream_->OnSample([this](Stream::Source::Type type, uint64_t time, std::vector<std::byte> const& sample) {
    SendSampleToPeers(sample, type, time);
  });
}

void MediaCallService::Configure(Config const& config) {
  INVARIANT(av_stream_);
  av_stream_->Video()->Configure(config);
  av_stream_->Audio()->Configure(config);
}

void MediaCallService::StartCallInternal(ContactID const& peer_id, bool is_video) {
  auto const call_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

  // Log system message about call request
  INVARIANT(messaging_service_);
  nlohmann::json const msg = {{"action", "call_request"}, {"kind", call_kind}};
  messaging_service_->SendSystemMessage(peer_id, msg.dump());
}

void MediaCallService::StartVoiceCall(ContactID const& peer_id) { StartCallInternal(peer_id, false); }

void MediaCallService::StartVideoCall(ContactID const& peer_id) { StartCallInternal(peer_id, true); }

void MediaCallService::AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind) {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer) {
    LGW << "Cannot accept call from " << peer_id.ToString() << ", peer object not found.";
    return;
  }

  // Notify peer
  INVARIANT(messaging_service_);
  nlohmann::json const msg = {{"action", "call_accept"}, {"kind", call_kind}};
  messaging_service_->SendSystemMessage(peer_id, msg.dump());

  // Set up local media
  bool const is_video = (call_kind == messaging::HistoryEntryKind::videoCall);
  INVARIANT(av_stream_);
  av_stream_->Start();

  if (!peer->has_media_peer_connection) {
    transport_->CreateMediaPeerConnection(peer_id);
    peer->has_media_peer_connection = true;
  }

  peer->audio = transport_->AddAudio(peer_id, [this, peer_id]() {
    if (auto p = transport_->GetPeer(peer_id)) {
      transport_->AddToStream(p, false);
    }
  });

  if (is_video) {
    peer->video = transport_->AddVideo(peer_id, [this, peer_id]() {
      if (auto p = transport_->GetPeer(peer_id)) {
        transport_->AddToStream(p, true);
      }
    });
  }

  transport_->CreateOffer(peer_id, "media");

  INVARIANT(on_call_accepted_);
  on_call_accepted_(peer_id);
}

void MediaCallService::RejectCall(ContactID const& peer_id) { Hangup(peer_id, true); }

void MediaCallService::Hangup(ContactID const& peer_id, bool notify_peer) {
  auto peer = transport_->GetPeer(peer_id);
  if (peer) {
    peer->pending_offer_sdp.reset();
    peer->pending_candidates.clear();

    if (peer->has_media_peer_connection) {
      transport_->ClosePeerConnection(peer_id, "media");
      peer->has_media_peer_connection = false;
      peer->audio = nullptr;
      peer->video = nullptr;
      INVARIANT(av_stream_);
      av_stream_->Stop();
    }

    if (notify_peer && peer->data_channel_is_open) {
      transport_->SendPeerDataChannelMessage(peer_id, {{"type", "hangup"}});
    }
  }

  INVARIANT(on_call_terminated_);
  on_call_terminated_(peer_id);
}

void MediaCallService::OnFrameReceived(ContactID const& /*peer_id*/, std::vector<std::byte> const& frame) {
  INVARIANT(on_frame_received_);
  on_frame_received_(frame);
}

void MediaCallService::OnCallRequest(ContactID const& peer_id, messaging::HistoryEntryKind kind) {
  INVARIANT(on_incoming_call_);
  on_incoming_call_(peer_id, kind);

  // Auto-respond with "ringing"
  INVARIANT(messaging_service_);
  nlohmann::json const msg = {{"action", "call_ringing"}};
  messaging_service_->SendSystemMessage(peer_id, msg.dump());
}

void MediaCallService::OnCallRinging(ContactID const& peer_id) {
  INVARIANT(on_call_ringing_);
  on_call_ringing_(peer_id);
}

void MediaCallService::OnCallAccepted(ContactID const& peer_id, messaging::HistoryEntryKind kind) {
  INVARIANT(on_call_accepted_);
  on_call_accepted_(peer_id);

  bool const is_video = (kind == messaging::HistoryEntryKind::videoCall);

  INVARIANT(av_stream_);
  av_stream_->Start();

  auto peer = transport_->GetPeer(peer_id);
  if (!peer) {
    return;
  }

  transport_->CreateMediaPeerConnection(peer_id);
  peer->has_media_peer_connection = true;

  peer->audio = transport_->AddAudio(peer_id, [this, peer_id]() {
    if (auto p = transport_->GetPeer(peer_id)) {
      transport_->AddToStream(p, false);
    }
  });

  if (is_video) {
    peer->video = transport_->AddVideo(peer_id, [this, peer_id]() {
      if (auto p = transport_->GetPeer(peer_id)) {
        transport_->AddToStream(p, true);
      }
    });
  }

  transport_->CreateOffer(peer_id, "media");
}

void MediaCallService::OnMediaPeerConnectionStateChange(
    ContactID const& peer_id,
    transport::PeerConnectionState state
) {
  if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
      state == transport::PeerConnectionState::disconnected) {
    // If the media connection drops, we interpret it as the call ending
    Hangup(peer_id, false);
  }
}

void MediaCallService::SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type type, uint64_t time)
    const {
  for (auto const& [id, peer] : transport_->GetPeers()) {
    if (peer->GetState() == transport::Peer::State::ready) {
      auto track = (type == Stream::Source::Type::video) ? peer->video : peer->audio;
      if (track) {
        track->SendSample(sample, time);
      }
    }
  }
}

// Callbacks
void MediaCallService::SetIncomingCallCallback(OnIncomingCallCallback cb) { on_incoming_call_ = std::move(cb); }
void MediaCallService::SetCallRingingCallback(OnCallRingingCallback cb) { on_call_ringing_ = std::move(cb); }
void MediaCallService::SetCallAcceptedCallback(OnCallAcceptedCallback cb) { on_call_accepted_ = std::move(cb); }
void MediaCallService::SetCallTerminatedCallback(OnCallTerminatedCallback cb) { on_call_terminated_ = std::move(cb); }
void MediaCallService::SetFrameReceivedCallback(OnFrameReceivedCallback cb) { on_frame_received_ = std::move(cb); }

}  // namespace eillance::services
