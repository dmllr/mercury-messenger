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
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

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

  explicit MediaCallService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::DataTransport> data_transport,
      std::shared_ptr<transport::MediaTransport> media_transport,
      std::shared_ptr<AccountService> account_service
  )
      : storage_(std::move(storage)),
        data_transport_(std::move(data_transport)),
        media_transport_(std::move(media_transport)),
        account_service_(std::move(account_service)) {
    auto sub_dco = EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      this->OnDataChannelOpen(e.peer_id);
    });
    event_unsubs_.push_back([sub_dco]() { EventQueue::Instance().Unsubscribe<EventDataChannelOpen>(sub_dco); });

    auto sub_ms = EventQueue::Instance().Subscribe<EventMediaSample>([this](auto const& e) {
      this->SendSampleToPeers(e.sample, static_cast<Stream::Source::Type>(e.type), e.timestamp);
    });
    event_unsubs_.push_back([sub_ms]() { EventQueue::Instance().Unsubscribe<EventMediaSample>(sub_ms); });

    auto sub_fr = EventQueue::Instance().Subscribe<EventFrameReceived>([this](auto const& e) {
      this->OnFrameReceived(e.peer_id, e.frame);
    });
    event_unsubs_.push_back([sub_fr]() { EventQueue::Instance().Unsubscribe<EventFrameReceived>(sub_fr); });
  }

  ~MediaCallService() {
    for (auto const& unsub : event_unsubs_) unsub();
  }

  void SetMessagingService(std::shared_ptr<MessagingService> messaging_service) {
    messaging_service_ = std::move(messaging_service);
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

    // Send accept signal
    nlohmann::json const msg = {{"type", "call_accept"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction(peer_id, msg.dump());
        if (messaging_service_) {
          messaging_service_->SendQueuedMessages(peer_id);
        }
      }
    }

    // Note: We used to add a system message here, but it's redundant with the final call log entry.

    EventQueue::Instance().Push(EventCallAccepted{peer_id});
  }

  void RejectCall(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!current_call_) {
      return;
    }

    // Send explicit reject signal
    nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto const peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction(peer_id, msg.dump());
        if (messaging_service_) {
          messaging_service_->SendQueuedMessages(peer_id);
        }
      }
    }

    TeardownCallInternal(peer_id);

    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  void Hangup(ContactID const& peer_id, bool const notify_peer) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    // Only hangup if we are actually tracking a call with this peer
    if (current_call_ && current_call_->peer_id != peer_id) {
      return;
    }

    if (notify_peer && current_call_) {
      nlohmann::json const msg = {{"type", "hangup"}, {"call_id", current_call_->call_id.ToString()}};
      if (auto peer = data_transport_->GetPeer(peer_id)) {
        if (peer->data_channel_is_open) {
          std::ignore = data_transport_->SendJson(peer_id, msg);
        } else {
          storage_->AddPendingAction(peer_id, msg.dump());
          INVARIANT(messaging_service_ != nullptr);
          messaging_service_->SendQueuedMessages(peer_id);
        }
      }
    }

    TeardownCallInternal(peer_id);

    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  // --- Signals from Peer ---

  void OnCallRequest(ContactID const& peer_id, std::string const& call_id, messaging::HistoryEntryKind const kind) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    // If we are already in a call/busy, auto-reject
    if (current_call_) {
      // If we have a pending session from OnMediaOffer (call_id is empty), adopt it
      if (current_call_->peer_id == peer_id && current_call_->call_id.IsEmpty()) {
        current_call_->call_id = CallID(call_id);
        // If media happened to connect extremely fast (before this request processed), ring now
        if (current_call_->state == CallState::mediaConnected) {
          TriggerIncomingRing(peer_id);
          SetCallState(CallState::ringing);
        }
        return;
      }

      // If it's a re-transmission of the *current* call request, ignore
      if (current_call_->call_id.ToString() == call_id && current_call_->peer_id == peer_id) {
        return;
      }

      nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", call_id}, {"reason", "busy"}};
      if (auto peer = data_transport_->GetPeer(peer_id)) {
        if (peer->data_channel_is_open) {
          std::ignore = data_transport_->SendJson(peer_id, msg);
        } else {
          storage_->AddPendingAction(peer_id, msg.dump());
          if (messaging_service_) {
            messaging_service_->SendQueuedMessages(peer_id);
          }
        }
      }
      return;
    }

    // New incoming call
    bool const is_video = (kind == messaging::HistoryEntryKind::videoCall);

    current_call_ = CallSession{
        .call_id = CallID(call_id),
        .peer_id = peer_id,
        .is_outgoing = false,
        .is_video = is_video,
        .state = CallState::mediaConnecting  // Wait for media to stabilize before ringing
    };

    // Prepare Media (Answer side)
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

  // --- Media Signaling (Offer/Answer via Data Channel) ---

  void OnMediaOffer(ContactID const& peer_id, std::string const& sdp) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    // Implicitly start session if receiving media offer and idle (race condition handling)
    if (!current_call_) {
      bool const is_video = sdp.find("m=video") != std::string::npos;
      current_call_ = CallSession{
          .call_id = CallID(),  // Pending Call ID
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

  // --- Data Channel Events ---

  void OnDataChannelOpen(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id == peer_id && current_call_->state == CallState::waitingForData) {
      LGI << "Data channel opened for call " << current_call_->call_id.ToString() << ". Proceeding to media.";
      StartMediaConnection(peer_id, current_call_->is_video, true);
    }
  }

  // --- Transport Events ---

  void OnMediaGatheringStateChange(ContactID const& peer_id, int state_val) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (state_val == 2) {  // Complete
      if (current_call_ && current_call_->peer_id == peer_id) {
        if (auto peer = data_transport_->GetPeer(peer_id)) {
          if (peer->pending_offer_sdp) {
            std::string const type = current_call_->is_outgoing ? "offer" : "answer";
            nlohmann::json const msg = {{"type", type}, {"context", "media"}, {"sdp", *peer->pending_offer_sdp}};
            if (peer->data_channel_is_open) {
              std::ignore = data_transport_->SendJson(peer_id, msg);
            } else {
              storage_->AddPendingAction(peer_id, msg.dump());
              if (messaging_service_) {
                messaging_service_->SendQueuedMessages(peer_id);
              }
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
            // Initiator: Connection established, now negotiate call intention
            SendCallRequest(peer_id);
            SetCallState(CallState::negotiating);
          } else {
            // Receiver: Connection established, if we have the Call ID, start ringing
            if (!current_call_->call_id.IsEmpty()) {
              TriggerIncomingRing(peer_id);
              SetCallState(CallState::ringing);
            }
            // Else wait for OnCallRequest to fill ID and trigger ring
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

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
  std::shared_ptr<AccountService> account_service_;
  std::shared_ptr<MessagingService> messaging_service_{nullptr};
  std::vector<std::function<void()>> event_unsubs_;

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
    auto const local_id = account_service_->GetId().ToString().substr(0, 8);
    auto const remote_id = remote.ToString().substr(0, 8);

    auto const& [first, second] =
        (local_id <= remote_id) ? std::tie(local_id, remote_id) : std::tie(remote_id, local_id);

    auto const timestamp = static_cast<uint64_t>(Time::Now());
    return CallID(first + "-" + second + "-" + std::to_string(timestamp));
  }

  void InitializeAvStream() {
    if (av_stream_) {
      return;
    }
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

    // Ensure Data Connection. If not open, we wait for OnDataChannelOpen event.
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

    // Force new media connection per call to ensure clean state
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
    // Note: Mutex is recursive, so we can lock here safely even if caller holds it.
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    // Force clear the session if it exists, regardless of peer_id match if we are forcing teardown.
    // However, to avoid killing a *new* call by rejecting an *old* one, we typically check peer_id.
    // But in this single-session architecture, stickiness causes bugs.
    // We check if the current call involves this peer.
    if (current_call_) {
      if (messaging_service_) {
        uint32_t duration = 0;
        if (current_call_->state == CallState::active) {
          auto now = std::chrono::steady_clock::now();
          duration = static_cast<uint32_t>(
              std::chrono::duration_cast<std::chrono::seconds>(now - current_call_->connected_at).count()
          );
        }

        // Only log if it matches the peer we are tearing down, otherwise it's a no-op log
        if (current_call_->peer_id == peer_id) {
          messaging_service_->AddCallEntry(
              peer_id,
              current_call_->is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall,
              current_call_->is_outgoing ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
              (duration > 0) ? messaging::CallStatus::answered : messaging::CallStatus::missed,
              duration
          );
        }
      }
      // Unconditionally reset the call session to prevent "stuck" UI states.
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
    // Send 'ringing' signal back to initiator
    nlohmann::json const msg = {{"type", "call_ringing"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto const peer = data_transport_->GetPeer(peer_id)) {
      if (peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      } else {
        storage_->AddPendingAction(peer_id, msg.dump());
        if (messaging_service_) {
          messaging_service_->SendQueuedMessages(peer_id);
        }
      }
    }

    // Notify UI
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
        storage_->AddPendingAction(peer_id, msg.dump());
        if (messaging_service_) {
          messaging_service_->SendQueuedMessages(peer_id);
        }
      }
    }
  }

  void SendSampleToPeers(std::vector<std::byte> const& sample, Stream::Source::Type const type, uint64_t const time)
      const {
    // Only send frames if call is Active
    // Note: Accessing current_call_ without lock is racy but usually benign for stream pumping loop.
    // If strict safety is needed, lock would cause jitter.
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
