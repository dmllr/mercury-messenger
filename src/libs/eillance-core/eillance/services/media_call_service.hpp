#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/capture/capture.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class MediaCallService {
 public:
  explicit MediaCallService(
      std::shared_ptr<transport::DataTransport> data_transport,
      std::shared_ptr<transport::MediaTransport> media_transport
  )
      : data_transport_(std::move(data_transport)),
        media_transport_(std::move(media_transport)) {
  }

  void SetMessagingService(MessagingService* messaging_service) {
    messaging_service_ = messaging_service;
  }

  template <typename T>
  void SetVideoCapture(std::unique_ptr<T> impl) {
    video_capture_ = std::make_unique<Capture>(std::move(impl));
    if (audio_capture_) {
      InitializeAvStream();
    }
  }

  template <typename T>
  void SetAudioCapture(std::unique_ptr<T> impl) {
    audio_capture_ = std::make_unique<Capture>(std::move(impl));
    if (video_capture_) {
      InitializeAvStream();
    }
  }

  void Configure(Config const& config) {
    if (av_stream_) {
      av_stream_->Video()->Configure(config);
      av_stream_->Audio()->Configure(config);
    }
  }

  void StartVoiceCall(ContactID const& peer_id) {
    StartCallInternal(peer_id, false);
  }
  void StartVideoCall(ContactID const& peer_id) {
    StartCallInternal(peer_id, true);
  }

  void AcceptCall(ContactID const& peer_id, messaging::HistoryEntryKind call_kind) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      LGW << "Cannot accept call from " << peer_id.ToString() << ", peer object not found.";
      return;
    }

    if (peer->data_channel_is_open) {
      if (!data_transport_->SendJson(peer_id, {{"type", "call_accept"}})) {
        LGW << "Failed to send call_accept to " << peer_id;
      }
    }

    INVARIANT(messaging_service_);
    nlohmann::json const msg = {{"action", "call_accept"}, {"kind", call_kind}};
    messaging_service_->AddSystemEntry(peer_id, msg.dump(), messaging::MessageDirection::sent);

    bool const is_video = (call_kind == messaging::HistoryEntryKind::videoCall);
    if (av_stream_) {
      av_stream_->Start();
    }

    media_transport_->Connect(peer_id);
    peer->has_media_peer_connection = true;

    peer->audio = media_transport_->AddAudio(peer_id, [this, peer_id]() -> void {
      if (auto p = data_transport_->GetPeer(peer_id)) {
        media_transport_->AttachToPeer(peer_id, p);
      }
    });

    if (is_video) {
      peer->video = media_transport_->AddVideo(peer_id, [this, peer_id]() -> void {
        if (auto p = data_transport_->GetPeer(peer_id)) {
          media_transport_->AttachToPeer(peer_id, p);
        }
      });
    }

    media_transport_->CreateAnswer(peer_id);

    if (on_call_accepted_) {
      on_call_accepted_(peer_id);
    }
  }

  void RejectCall(ContactID const& peer_id) {
    Hangup(peer_id, true);
  }

  void Hangup(ContactID const& peer_id, bool notify_peer) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (peer) {
      if (notify_peer) {
        if (peer->data_channel_is_open) {
          if (!data_transport_->SendJson(peer_id, {{"type", "hangup"}})) {
            LGW << "Hangup: Failed to send hangup message to " << peer_id << " (transport returned false)";
          } else {
            LGI << "Hangup: Sent hangup notification to " << peer_id;
          }
        } else {
          LGW << "Hangup: Cannot notify peer " << peer_id.ToString() << ", Data Channel is not open.";
        }
      }

      peer->pending_offer_sdp.reset();
      peer->pending_candidates.clear();

      if (peer->has_media_peer_connection) {
        peer->has_media_peer_connection = false;
        peer->audio = nullptr;
        peer->video = nullptr;
        if (av_stream_) {
          av_stream_->Stop();
        }

        media_transport_->Disconnect(peer_id);
        LGI << "Hangup: Closed media connection for " << peer_id;
      }
    } else {
      LGW << "Hangup: Peer " << peer_id << " not found.";
    }

    if (on_call_terminated_) {
      on_call_terminated_(peer_id);
    }
  }

  void OnFrameReceived(ContactID const& /*peer_id*/, std::vector<std::byte> const& frame) {
    if (on_frame_received_) {
      on_frame_received_(frame);
    }
  }

  void OnCallRequest(ContactID const& peer_id, messaging::HistoryEntryKind const kind) {
    if (on_incoming_call_) {
      on_incoming_call_(peer_id, kind);
    }

    // Auto-respond with "ringing" directly if P2P is established.
    if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      data_transport_->SendJson(peer_id, {{"type", "call_ringing"}});
    }
  }

  void OnCallRinging(ContactID const& peer_id) {
    if (on_call_ringing_) {
      on_call_ringing_(peer_id);
    }
  }

  void OnCallAccepted(ContactID const& peer_id, messaging::HistoryEntryKind const kind) {
    if (on_call_accepted_) {
      on_call_accepted_(peer_id);
    }

    bool const is_video = (kind == messaging::HistoryEntryKind::videoCall);

    if (av_stream_) {
      av_stream_->Start();
    }

    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) return;

    media_transport_->Connect(peer_id);
    peer->has_media_peer_connection = true;

    peer->audio = media_transport_->AddAudio(peer_id, [this, peer_id]() -> void {
      if (auto p = data_transport_->GetPeer(peer_id)) {
        media_transport_->AttachToPeer(peer_id, p);
      }
    });

    if (is_video) {
      peer->video = media_transport_->AddVideo(peer_id, [this, peer_id]() -> void {
        if (auto p = data_transport_->GetPeer(peer_id)) {
          media_transport_->AttachToPeer(peer_id, p);
        }
      });
    }

    media_transport_->CreateOffer(peer_id);
  }

  void OnMediaPeerConnectionStateChange(ContactID const& peer_id, transport::PeerConnectionState state) {
    if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
        state == transport::PeerConnectionState::disconnected) {
      Hangup(peer_id, false);
    }
  }

  using OnIncomingCallCallback = std::function<void(ContactID const&, messaging::HistoryEntryKind)>;
  using OnCallRingingCallback = std::function<void(ContactID const&)>;
  using OnCallAcceptedCallback = std::function<void(ContactID const&)>;
  using OnCallTerminatedCallback = std::function<void(ContactID const&)>;
  using OnFrameReceivedCallback = std::function<void(std::vector<std::byte> const&)>;

  void SetIncomingCallCallback(OnIncomingCallCallback cb) {
    on_incoming_call_ = std::move(cb);
  }
  void SetCallRingingCallback(OnCallRingingCallback cb) {
    on_call_ringing_ = std::move(cb);
  }
  void SetCallAcceptedCallback(OnCallAcceptedCallback cb) {
    on_call_accepted_ = std::move(cb);
  }
  void SetCallTerminatedCallback(OnCallTerminatedCallback cb) {
    on_call_terminated_ = std::move(cb);
  }
  void SetFrameReceivedCallback(OnFrameReceivedCallback cb) {
    on_frame_received_ = std::move(cb);
  }

 private:
  void InitializeAvStream() {
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

  void StartCallInternal(ContactID const& peer_id, bool is_video) {
    auto const call_kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

    INVARIANT(messaging_service_);
    nlohmann::json const msg = {{"action", "call_request"}, {"kind", call_kind}};
    messaging_service_->AddSystemEntry(peer_id, msg.dump(), messaging::MessageDirection::sent);

    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      data_transport_->AddPeer(peer_id, peer);
    }

    if (av_stream_) {
      av_stream_->Start();
    }

    media_transport_->Connect(peer_id);
    peer->has_media_peer_connection = true;

    peer->audio = media_transport_->AddAudio(peer_id, [this, peer_id]() -> void {
      if (auto p = data_transport_->GetPeer(peer_id)) {
        media_transport_->AttachToPeer(peer_id, p);
      }
    });

    if (is_video) {
      peer->video = media_transport_->AddVideo(peer_id, [this, peer_id]() -> void {
        if (auto p = data_transport_->GetPeer(peer_id)) {
          media_transport_->AttachToPeer(peer_id, p);
        }
      });
    }

    media_transport_->CreateOffer(peer_id);
  }

  void SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type type, uint64_t time) const {
    for (auto const& [id, peer] : data_transport_->GetPeers()) {
      if (peer->has_media_peer_connection) {
        if (auto track = (type == Stream::Source::Type::video) ? peer->video : peer->audio) {
          track->SendSample(sample, time);
        }
      }
    }
  }

  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
  MessagingService* messaging_service_{nullptr};

  std::unique_ptr<Capture> video_capture_;
  std::unique_ptr<Capture> audio_capture_;
  std::shared_ptr<Stream> av_stream_;

  OnIncomingCallCallback on_incoming_call_;
  OnCallRingingCallback on_call_ringing_;
  OnCallAcceptedCallback on_call_accepted_;
  OnCallTerminatedCallback on_call_terminated_;
  OnFrameReceivedCallback on_frame_received_;
};

}  // namespace eillance::services
