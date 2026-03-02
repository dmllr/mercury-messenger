#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/capture/capture.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/stream.hpp"
#include "eillance/rtc/stream_source.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/call_id.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

class MediaCallService {
 public:
  struct CallSessionInfo {
    CallID call_id;
    ContactID peer_id;
    bool is_outgoing;
    bool is_video;
    bool is_ringing;
    bool is_active;
  };

  explicit MediaCallService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        data_transport_(sm->data_transport),
        media_transport_(sm->media_transport) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      this->OnDataChannelOpen(e.peer_id);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventMediaSample>([this](auto const& e) {
      this->SendSampleToPeers(e.sample, static_cast<Stream::Source::Type>(e.type), e.timestamp);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventFrameReceived>([this](auto const& e) {
      this->OnFrameReceived(e.peer_id, e.frame);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventMediaPeerConnectionStateChanged>(
        [this](auto const& e) { this->OnMediaPeerConnectionStateChange(e.peer_id, e.state); }
    ));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventMediaGatheringStateChanged>([this](auto const& e) {
      this->OnMediaGatheringStateChange(e.peer_id, e.state_val);
    }));
  }

  ~MediaCallService() {
    for (auto const id : event_unsubs_) EventQueue::Instance().Unsubscribe(id);
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

  [[nodiscard]] auto GetCurrentSession() const -> std::optional<CallSessionInfo> {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!current_call_) {
      return std::nullopt;
    }
    return CallSessionInfo{
        .call_id = current_call_->call_id,
        .peer_id = current_call_->peer_id,
        .is_outgoing = current_call_->is_outgoing,
        .is_video = current_call_->is_video,
        .is_ringing = (current_call_->state == CallState::ringing),
        .is_active = (current_call_->state == CallState::active)
    };
  }

  // --- User Actions ---

  void StartVoiceCall(ContactID const& peer_id) {
    StartCallFlow(peer_id, false);
  }

  void StartVideoCall(ContactID const& peer_id) {
    StartCallFlow(peer_id, true);
  }

  void AcceptCall(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!current_call_ || current_call_->peer_id != peer_id || current_call_->state != CallState::ringing) {
      LGW << "No ringing call to accept for " << peer_id;
      return;
    }

    SetCallState(CallState::active);

    nlohmann::json const msg = {{"type", "call_accept"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      }
    }

    EventQueue::Instance().Push(EventCallAccepted{peer_id});
  }

  void RejectCall(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!current_call_) {
      return;
    }

    nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto const peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      }
    }

    TeardownCallInternal(peer_id);

    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  void Hangup(ContactID const& peer_id, bool const notify_peer) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id != peer_id) {
      return;
    }

    if (notify_peer && current_call_) {
      nlohmann::json const msg = {{"type", "hangup"}, {"call_id", current_call_->call_id.ToString()}};
      if (auto peer = data_transport_->GetPeer(peer_id)) {
        if (peer->data_channel_is_open) {
          std::ignore = data_transport_->SendJson(peer_id, msg);
        } else {
          storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
          EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
        }
      }
    }

    TeardownCallInternal(peer_id);
    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  // --- Signals from Peer ---

  void OnCallRequest(ContactID const& peer_id, std::string const& call_id, messaging::HistoryEntryKind const kind) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (current_call_) {
      if (current_call_->peer_id == peer_id && current_call_->call_id.IsEmpty()) {
        current_call_->call_id = CallID(call_id);
        if (current_call_->state == CallState::mediaConnected) {
          TriggerIncomingRing(peer_id);
          SetCallState(CallState::ringing);
        }
        return;
      }

      if (current_call_->call_id.ToString() == call_id && current_call_->peer_id == peer_id) {
        return;
      }

      nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", call_id}, {"reason", "busy"}};
      if (auto peer = data_transport_->GetPeer(peer_id)) {
        if (peer->data_channel_is_open) {
          std::ignore = data_transport_->SendJson(peer_id, msg);
        } else {
          storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
          EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
        }
      }
      return;
    }

    bool const is_video = (kind == messaging::HistoryEntryKind::videoCall);

    current_call_ = CallSession{
        .call_id = CallID(call_id),
        .peer_id = peer_id,
        .is_outgoing = false,
        .is_video = is_video,
        .state = CallState::mediaConnecting
    };

    StartMediaConnection(peer_id, is_video, false);
  }

  void OnCallRinging(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    if (current_call_->state == CallState::mediaConnected || current_call_->state == CallState::negotiating) {
      SetCallState(CallState::ringing);
      EventQueue::Instance().Push(EventCallRinging{peer_id});
    }
  }

  void OnCallAccepted(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    SetCallState(CallState::active);
    EventQueue::Instance().Push(EventCallAccepted{peer_id});
  }

  void OnCallReject(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    LGI << "Call " << call_id << " rejected by peer " << peer_id;
    TeardownCallInternal(peer_id);
    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  void OnHangupSignal(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    LGI << "Call " << call_id << " hung up by peer " << peer_id;
    TeardownCallInternal(peer_id);
    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  // --- Media Signaling ---

  void OnMediaOffer(ContactID const& peer_id, std::string const& sdp) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (!current_call_) {
      bool const is_video = sdp.find("m=video") != std::string::npos;
      current_call_ = CallSession{
          .call_id = CallID(),
          .peer_id = peer_id,
          .is_outgoing = false,
          .is_video = is_video,
          .state = CallState::mediaConnecting
      };
      StartMediaConnection(peer_id, is_video, false);
    }

    if (current_call_ && current_call_->peer_id == peer_id) {
      media_transport_->SetRemoteDescription(peer_id, sdp, "offer");
      media_transport_->CreateAnswer(peer_id);
    }
  }

  void OnMediaAnswer(ContactID const& peer_id, std::string const& sdp) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id == peer_id) {
      media_transport_->SetRemoteDescription(peer_id, sdp, "answer");
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id == peer_id && current_call_->state == CallState::waitingForData) {
      LGI << "Data channel opened for call " << current_call_->call_id.ToString() << ". Proceeding to media.";
      StartMediaConnection(peer_id, current_call_->is_video, true);
    }
  }

  void OnMediaGatheringStateChange(ContactID const& peer_id, int state_val) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (state_val == 2) {
      if (current_call_ && current_call_->peer_id == peer_id) {
        if (auto peer = data_transport_->GetPeer(peer_id)) {
          if (peer->pending_offer_sdp) {
            std::string const type = current_call_->is_outgoing ? "offer" : "answer";
            nlohmann::json const msg = {{"type", type}, {"context", "media"}, {"sdp", *peer->pending_offer_sdp}};
            if (peer->data_channel_is_open) {
              std::ignore = data_transport_->SendJson(peer_id, msg);
            } else {
              storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
              EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
            }
            peer->pending_offer_sdp.reset();
          }
        }
      }
    }
  }

  void OnMediaPeerConnectionStateChange(ContactID const& peer_id, transport::PeerConnectionState state) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
        state == transport::PeerConnectionState::disconnected) {
      if (current_call_ && current_call_->peer_id == peer_id) {
        LGE << "Media connection dropped for call " << current_call_->call_id.ToString() << ". Forcing hangup.";
        TeardownCallInternal(peer_id);
        EventQueue::Instance().Push(EventCallTerminated{peer_id});
      }
      return;
    }

    if (state == transport::PeerConnectionState::connected) {
      if (current_call_ && current_call_->peer_id == peer_id) {
        if (current_call_->state == CallState::mediaConnecting) {
          SetCallState(CallState::mediaConnected);

          if (current_call_->is_outgoing) {
            SendCallRequest(peer_id);
            SetCallState(CallState::negotiating);
          } else {
            if (!current_call_->call_id.IsEmpty()) {
              TriggerIncomingRing(peer_id);
              SetCallState(CallState::ringing);
            }
          }
        }
      }
    }
  }

  void OnFrameReceived(ContactID const& peer_id, std::vector<std::byte> const& frame) {
    EventQueue::Instance().Push(EventFrameReceived{peer_id, frame});
  }

 private:
  enum class CallState { idle, waitingForData, mediaConnecting, mediaConnected, negotiating, ringing, active };

  struct CallSession {
    CallID call_id;
    ContactID peer_id;
    bool is_outgoing;
    bool is_video;
    CallState state = CallState::idle;
    std::chrono::steady_clock::time_point connected_at{};
  };

  std::optional<CallSession> current_call_;
  mutable std::recursive_mutex mutex_;

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unique_ptr<Capture> video_capture_;
  std::unique_ptr<Capture> audio_capture_;
  std::shared_ptr<Stream> av_stream_;

  void SetCallState(CallState state) {
    if (current_call_) {
      current_call_->state = state;
      if (state == CallState::active) {
        current_call_->connected_at = std::chrono::steady_clock::now();
      }
    }
  }

  void ResetCallState() {
    current_call_.reset();
  }

  [[nodiscard]] auto ValidateCallId(ContactID const& peer, std::string const& call_id) const -> bool {
    return current_call_ && current_call_->peer_id == peer && current_call_->call_id.ToString() == call_id;
  }

  [[nodiscard]] auto GenerateCallId(ContactID const& remote) const -> CallID {
    auto const local_id = sm_->account->GetId().ToString().substr(0, 8);
    auto const remote_id = remote.ToString().substr(0, 8);
    auto const& [first, second] =
        (local_id <= remote_id) ? std::tie(local_id, remote_id) : std::tie(remote_id, local_id);
    auto const timestamp = static_cast<uint64_t>(Time::Now());
    return CallID(first + "-" + second + "-" + std::to_string(timestamp));
  }

  void InitializeAvStream() {
    if (av_stream_) return;
    if (video_capture_ && audio_capture_) {
      av_stream_ = std::make_shared<Stream>(
          std::make_shared<StreamSource>(std::move(*video_capture_)),
          std::make_shared<StreamSource>(std::move(*audio_capture_))
      );
    }
  }

  void StartCallFlow(ContactID const& peer_id, bool const is_video) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_) {
      LGW << "Call already active, cannot start new one.";
      return;
    }

    current_call_ = CallSession{
        .call_id = GenerateCallId(peer_id),
        .peer_id = peer_id,
        .is_outgoing = true,
        .is_video = is_video,
        .state = CallState::waitingForData
    };

    auto const kind = is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;
    EventQueue::Instance().Push(EventOutgoingCall{peer_id, kind});

    auto const peer = data_transport_->GetPeer(peer_id);
    if (!peer || !peer->data_channel_is_open) {
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
      return;
    }

    StartMediaConnection(peer_id, is_video, true);
  }

  void StartMediaConnection(ContactID const& peer_id, bool is_video, bool is_initiator) {
    if (av_stream_) {
      av_stream_->Start();
    }

    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      data_transport_->AddPeer(peer_id, peer);
    }

    if (auto target_peer = data_transport_->GetPeer(peer_id)) {
      media_transport_->AttachToPeer(peer_id, target_peer);
    }

    media_transport_->Disconnect(peer_id);
    media_transport_->Connect(peer_id);

    peer->has_media_peer_connection = true;
    SetCallState(CallState::mediaConnecting);

    peer->audio = media_transport_->AddAudio(peer_id);
    if (is_video) {
      peer->video = media_transport_->AddVideo(peer_id);
    }

    if (is_initiator) {
      media_transport_->CreateOffer(peer_id);
    }
  }

  void TeardownCallInternal(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (current_call_) {
      uint32_t duration = 0;
      if (current_call_->state == CallState::active) {
        auto now = std::chrono::steady_clock::now();
        duration = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - current_call_->connected_at).count()
        );
      }

      if (current_call_->peer_id == peer_id) {
        sm_->messaging->AddCallEntry(
            {.peer_id = peer_id,
             .kind = current_call_->is_video ? messaging::HistoryEntryKind::videoCall
                                             : messaging::HistoryEntryKind::voiceCall,
             .direction =
                 current_call_->is_outgoing ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
             .status = (duration > 0) ? messaging::CallStatus::answered : messaging::CallStatus::missed,
             .duration_seconds = duration}
        );
      }
      current_call_.reset();
    }

    if (auto peer = data_transport_->GetPeer(peer_id)) {
      peer->has_media_peer_connection = false;
      peer->audio = nullptr;
      peer->video = nullptr;
      peer->pending_offer_sdp.reset();
      peer->pending_candidates.clear();
    }

    media_transport_->Disconnect(peer_id);

    if (av_stream_) {
      av_stream_->Stop();
    }
  }

  void TriggerIncomingRing(ContactID const& peer_id) {
    nlohmann::json const msg = {{"type", "call_ringing"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto const peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      }
    }

    messaging::HistoryEntryKind const kind =
        current_call_->is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

    EventQueue::Instance().Push(EventIncomingCall{peer_id, kind});
  }

  void SendCallRequest(ContactID const& peer_id) {
    auto const call_kind =
        current_call_->is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall;

    nlohmann::json const msg = {
        {"type", "call_request"},
        {"call_id", current_call_->call_id.ToString()},
        {"kind", static_cast<int>(call_kind)}
    };

    if (auto const peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction({.peer_id = peer_id, .payload = msg.dump()});
        EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
      }
    }
  }

  void SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type const type, uint64_t const time)
      const {
    if (current_call_ && current_call_->state == CallState::active) {
      ContactID const& peer_id = current_call_->peer_id;
      if (auto const peer = data_transport_->GetPeer(peer_id)) {
        if (auto const track = (type == Stream::Source::Type::video) ? peer->video : peer->audio) {
          track->SendSample(sample, time);
        }
      }
    }
  }
};

}  // namespace eillance::services
