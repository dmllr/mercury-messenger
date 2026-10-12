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

#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/call_id.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/utils/dispatch_queue.hpp"
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
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) -> void {
      this->OnDataChannelOpen(e.peer_id);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventMediaPeerConnectionStateChanged>(
        [this](auto const& e) { this->OnMediaPeerConnectionStateChange(e.peer_id, e.state); }
    ));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventMediaGatheringStateChanged>(
        [this](auto const& e) -> void { this->OnMediaGatheringStateChange(e.peer_id, e.state_val); }
    ));
  }

  ~MediaCallService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
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

    media_transport_->StartLocalMedia(current_call_->is_video);
    SetCallState(CallState::active);

    nlohmann::json const msg = {{"type", "call_accept"}, {"call_id", current_call_->call_id.ToString()}};

    if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      std::ignore = data_transport_->SendJson(peer_id, msg);
    }

    EventQueue::Instance().Push(EventCallAccepted{peer_id});
  }

  void RejectCall(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!current_call_) {
      return;
    }

    nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      std::ignore = data_transport_->SendJson(peer_id, msg);
    }

    TeardownCallInternal(peer_id, messaging::CallStatus::rejected);

    EventQueue::Instance().Push(EventCallTerminated{peer_id});
  }

  void Hangup(ContactID const& peer_id, bool const notify_peer) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id != peer_id) {
      return;
    }

    if (notify_peer && current_call_) {
      nlohmann::json const msg = {{"type", "hangup"}, {"call_id", current_call_->call_id.ToString()}};
      if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
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
        TriggerIncomingRing(peer_id);
        SetCallState(CallState::ringing);
        return;
      }

      if (current_call_->call_id.ToString() == call_id && current_call_->peer_id == peer_id) {
        return;
      }

      nlohmann::json const msg = {{"type", "call_reject"}, {"call_id", call_id}, {"reason", "busy"}};
      if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
        std::ignore = data_transport_->SendJson(peer_id, msg);
      }
      return;
    }

    bool const is_video = (kind == messaging::HistoryEntryKind::videoCall);

    current_call_ = CallSession{
        .call_id = CallID(call_id),
        .peer_id = peer_id,
        .is_outgoing = false,
        .is_video = is_video,
        .state = CallState::ringing
    };

    TriggerIncomingRing(peer_id);

    data_transport_->DiscoverTurnRelays();
    call_queue_.DispatchAfter(std::chrono::milliseconds(1500), [this, peer_id, is_video]() {
      std::lock_guard<std::recursive_mutex> const lock(mutex_);
      if (current_call_ && current_call_->peer_id == peer_id) {
        StartMediaConnection(peer_id, is_video, false);
      }
    });
  }

  void OnCallRinging(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    SetCallState(CallState::ringing);
    EventQueue::Instance().Push(EventCmdPlaySound{SoundType::ringback});
    EventQueue::Instance().Push(EventCallRinging{peer_id});
  }

  void OnCallAccepted(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    SetCallState(CallState::active);
    EventQueue::Instance().Push(EventCmdStopSound{});
    EventQueue::Instance().Push(EventCallAccepted{peer_id});
  }

  void OnCallReject(ContactID const& peer_id, std::string const& call_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (!ValidateCallId(peer_id, call_id)) {
      return;
    }

    LGI << "Call " << call_id << " rejected by peer " << peer_id;
    TeardownCallInternal(peer_id, messaging::CallStatus::rejected);
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
    LGD << "[WebRTC] Received Remote Offer SDP:\n" << sdp;

    if (!current_call_) {
      bool const is_video = sdp.find("m=video") != std::string::npos;
      current_call_ = CallSession{
          .call_id = CallID(),
          .peer_id = peer_id,
          .is_outgoing = false,
          .is_video = is_video,
          .state = CallState::mediaConnecting
      };

      data_transport_->DiscoverTurnRelays();
      call_queue_.DispatchAfter(std::chrono::milliseconds(1500), [this, peer_id, is_video, sdp]() {
        std::lock_guard<std::recursive_mutex> const lock(mutex_);
        if (current_call_ && current_call_->peer_id == peer_id) {
          StartMediaConnection(peer_id, is_video, false);
          try {
            media_transport_->SetRemoteDescription(peer_id, sdp, "offer");
            media_transport_->CreateAnswer(peer_id);
            call_queue_.DispatchAfter(std::chrono::milliseconds(2500), [this, peer_id]() {
              ForceSendLocalDescription(peer_id);
            });
          } catch (std::exception const& e) {
            LGE << "Failed to process media offer: " << e.what();
          }
        }
      });
      return;
    }

    if (current_call_ && current_call_->peer_id == peer_id) {
      try {
        LGI << "[WebRTC] Applying remote offer SDP from " << peer_id.ToString();
        media_transport_->SetRemoteDescription(peer_id, sdp, "offer");
        media_transport_->CreateAnswer(peer_id);
        call_queue_.DispatchAfter(std::chrono::milliseconds(2500), [this, peer_id]() {
          ForceSendLocalDescription(peer_id);
        });
      } catch (std::exception const& e) {
        LGE << "Failed to process media offer: " << e.what();
      }
    }
  }

  void OnMediaAnswer(ContactID const& peer_id, std::string const& sdp) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    LGD << "[WebRTC] Received Remote Answer SDP:\n" << sdp;

    if (current_call_ && current_call_->peer_id == peer_id) {
      try {
        LGI << "[WebRTC] Applying remote answer SDP from " << peer_id.ToString();
        media_transport_->SetRemoteDescription(peer_id, sdp, "answer");
      } catch (std::exception const& e) {
        LGE << "Failed to set media answer: " << e.what();
      }
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id == peer_id && current_call_->state == CallState::waitingForData) {
      LGI << "Data channel opened for call " << current_call_->call_id.ToString() << ". Proceeding to signaling.";
      SetCallState(CallState::negotiating);
      SendCallRequest(peer_id);

      data_transport_->DiscoverTurnRelays();
      call_queue_.DispatchAfter(std::chrono::milliseconds(1500), [this, peer_id, is_video = current_call_->is_video]() {
        std::lock_guard<std::recursive_mutex> const lock(mutex_);
        if (current_call_ && current_call_->peer_id == peer_id) {
          StartMediaConnection(peer_id, is_video, true);
        }
      });
    }
  }

  void OnMediaGatheringStateChange(ContactID const& peer_id, int state_val) {
    LGI << "[WebRTC] ICE Gathering state changed to " << state_val << " for " << peer_id.ToString();
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (state_val == 2) {
      if (current_call_ && current_call_->peer_id == peer_id) {
        if (auto peer = data_transport_->GetPeer(peer_id)) {
          if (peer->pending_offer_sdp && peer->pending_offer_sdp->empty()) {
            LGI << "[WebRTC] SDP was already sent via timeout. Ignoring late completion.";
            return;
          }
          std::optional<std::string> sdp = peer->pending_offer_sdp;
          if (!sdp) {
            try {
              sdp = media_transport_->GetLocalDescription(peer_id);
            } catch (std::exception const& e) {
              LGE << "Failed to get local description: " << e.what();
            }
          }

          if (sdp && !sdp->empty()) {
            std::string const type = current_call_->is_outgoing ? "offer" : "answer";
            LGI << "[WebRTC] ICE Gathering complete. Sending " << type << " to " << peer_id.ToString();
            LGD << "[WebRTC] Generated Local SDP:\n" << *sdp;
            nlohmann::json const msg = {{"type", type}, {"context", "media"}, {"sdp", *sdp}};
            if (peer->data_channel_is_open) {
              std::ignore = data_transport_->SendJson(peer_id, msg);
            }
            peer->pending_offer_sdp = "";  // Mark as sent
          }
        }
      }
    }
  }

  void OnMediaPeerConnectionStateChange(ContactID const& peer_id, transport::PeerConnectionState state) {
    LGD << "[WebRTC] PeerConnection state changed to " << static_cast<int>(state) << " for " << peer_id.ToString();
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (state == transport::PeerConnectionState::failed || state == transport::PeerConnectionState::closed ||
        state == transport::PeerConnectionState::disconnected) {
      auto const current_state = media_transport_->GetConnectionState(peer_id);
      if (current_state != transport::PeerConnectionState::failed &&
          current_state != transport::PeerConnectionState::closed &&
          current_state != transport::PeerConnectionState::disconnected) {
        LGD << "Ignoring stale media connection state event for " << peer_id.ToString();
        return;
      }

      if (current_call_ && current_call_->peer_id == peer_id) {
        LGE << "Media connection dropped for call " << current_call_->call_id.ToString()
            << ". State: " << static_cast<int>(state) << ". Forcing hangup.";
        TeardownCallInternal(peer_id);
        EventQueue::Instance().Push(EventCallTerminated{peer_id});
      }
      return;
    }

    if (state == transport::PeerConnectionState::connected) {
      if (current_call_ && current_call_->peer_id == peer_id) {
        LGI << "Media connected for call " << current_call_->call_id.ToString();
      }
    }
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

  DispatchQueue call_queue_{"MediaCall", 1};

  void SetCallState(CallState state) {
    if (current_call_) {
      current_call_->state = state;
      if (state == CallState::active) {
        current_call_->connected_at = std::chrono::steady_clock::now();
        media_transport_->SetLocalMediaEnabled(true, current_call_->is_video);
        EventQueue::Instance().Push(EventCmdSetTelecomCallActive{current_call_->peer_id});
        EventQueue::Instance().Push(EventCmdSetAudioMode{AudioRoutingMode::inCall});
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

    // Hardware Orchestration Commands
    media_transport_->StartLocalMedia(is_video);
    EventQueue::Instance().Push(EventCmdProcessOutgoingTelecomCall{peer_id, is_video});
    EventQueue::Instance().Push(EventCmdPlaySound{SoundType::connecting});

    auto const peer = data_transport_->GetPeer(peer_id);
    if (!peer || !peer->data_channel_is_open) {
      EventQueue::Instance().Push(EventConnectionRequested{peer_id});
      return;
    }

    SetCallState(CallState::negotiating);
    SendCallRequest(peer_id);

    data_transport_->DiscoverTurnRelays();
    call_queue_.DispatchAfter(std::chrono::milliseconds(1500), [this, peer_id, is_video]() -> void {
      std::lock_guard<std::recursive_mutex> const dispatch_lock(mutex_);
      if (current_call_ && current_call_->peer_id == peer_id) {
        StartMediaConnection(peer_id, is_video, true);
      }
    });
  }

  void StartMediaConnection(ContactID const& peer_id, bool const is_video, bool const is_initiator) {
    LGI << "[MediaCall] Starting WebRTC media connection (video=" << is_video << ", initiator=" << is_initiator
        << ") for " << peer_id.ToString();

    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      data_transport_->AddPeer(peer_id, peer);
    }

    media_transport_->Disconnect(peer_id);
    media_transport_->Connect(peer_id);

    peer->has_media_peer_connection = true;
    peer->pending_offer_sdp.reset();

    media_transport_->AddAudio(peer_id);
    if (is_video) {
      media_transport_->AddVideo(peer_id);
    }

    if (is_initiator) {
      try {
        media_transport_->CreateOffer(peer_id);
        call_queue_.DispatchAfter(std::chrono::milliseconds(2500), [this, peer_id]() {
          ForceSendLocalDescription(peer_id);
        });
      } catch (std::exception const& e) {
        LGE << "Failed to create offer: " << e.what();
      }
    }
  }

  void ForceSendLocalDescription(ContactID const& peer_id) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);
    if (current_call_ && current_call_->peer_id == peer_id) {
      if (auto peer = data_transport_->GetPeer(peer_id)) {
        if (!peer->has_media_peer_connection) {
          return;
        }
        if (!peer->pending_offer_sdp) {
          std::optional<std::string> sdp;
          try {
            sdp = media_transport_->GetLocalDescription(peer_id);
          } catch (std::exception const& e) {
            LGE << "Failed to get local description for forced send: " << e.what();
          }
          if (sdp && !sdp->empty()) {
            std::string const type = current_call_->is_outgoing ? "offer" : "answer";
            LGI << "[WebRTC] Forced sending " << type << " to " << peer_id.ToString() << " after ICE timeout.";
            LGD << "[WebRTC] Generated Local SDP:\n" << *sdp;
            nlohmann::json const msg = {{"type", type}, {"context", "media"}, {"sdp", *sdp}};
            if (peer->data_channel_is_open) {
              std::ignore = data_transport_->SendJson(peer_id, msg);
            }
            peer->pending_offer_sdp = "";  // Mark as sent
          }
        }
      }
    }
  }

  void
  TeardownCallInternal(ContactID const& peer_id, std::optional<messaging::CallStatus> explicit_status = std::nullopt) {
    std::lock_guard<std::recursive_mutex> const lock(mutex_);

    if (current_call_) {
      uint32_t duration = 0;
      if (current_call_->state == CallState::active) {
        auto now = std::chrono::steady_clock::now();
        duration = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(now - current_call_->connected_at).count()
        );
      }

      auto final_status = messaging::CallStatus::missed;
      if (explicit_status) {
        final_status = *explicit_status;
      } else if (duration > 0) {
        final_status = messaging::CallStatus::answered;
      }

      if (current_call_->peer_id == peer_id) {
        messaging::CallEntryParams const params{
            .peer_id = peer_id,
            .kind = current_call_->is_video ? messaging::HistoryEntryKind::videoCall
                                            : messaging::HistoryEntryKind::voiceCall,
            .direction =
                current_call_->is_outgoing ? messaging::CallDirection::outgoing : messaging::CallDirection::incoming,
            .status = final_status,
            .duration_seconds = duration
        };
        sm_->messaging->AddCallEntry(params, current_call_->is_outgoing);
        if (!current_call_->is_outgoing && final_status == messaging::CallStatus::missed) {
          if (!sm_->is_foreground) {
            auto const contact = sm_->contact_manager->GetContact(peer_id);
            std::string const name =
                contact && contact->display_name ? *contact->display_name : peer_id.ToString().substr(0, 8);
            EventQueue::Instance().Push(EventNotifyMissedCall{peer_id, name});
          }
        }
      }
      current_call_.reset();
    }

    // Hardware Orchestration Commands (Teardown)
    media_transport_->StopLocalMedia();
    EventQueue::Instance().Push(EventCmdDisconnectTelecomCall{peer_id});
    EventQueue::Instance().Push(EventCmdSetAudioMode{AudioRoutingMode::normal});
    EventQueue::Instance().Push(EventCmdClearCommunicationDevice{});
    EventQueue::Instance().Push(EventCmdStopSound{});

    if (auto peer = data_transport_->GetPeer(peer_id)) {
      peer->has_media_peer_connection = false;
      peer->pending_offer_sdp.reset();
    }

    media_transport_->Disconnect(peer_id);
  }

  void TriggerIncomingRing(ContactID const& peer_id) {
    nlohmann::json const msg = {{"type", "call_ringing"}, {"call_id", current_call_->call_id.ToString()}};
    if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      std::ignore = data_transport_->SendJson(peer_id, msg);
    }

    // Hardware Orchestration
    EventQueue::Instance().Push(EventCmdProcessIncomingTelecomCall{peer_id});
    EventQueue::Instance().Push(EventCmdSetAudioMode{AudioRoutingMode::normal});
    EventQueue::Instance().Push(EventCmdPlaySound{SoundType::ringing});

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

    if (auto peer = data_transport_->GetPeer(peer_id); peer && peer->data_channel_is_open) {
      std::ignore = data_transport_->SendJson(peer_id, msg);
    }
  }
};

}  // namespace eillance::services
