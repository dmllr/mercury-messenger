#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/utils/event_queue.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class CallViewModel {
 public:
  explicit CallViewModel(std::shared_ptr<Client> client)
      : impl_(std::make_shared<Impl>(std::move(client))) {
    auto& queue = EventQueue::Instance();
    auto impl = impl_;
    Subscribe<EventIncomingCall>(queue, [impl](auto const& event) {
      impl->OnIncomingCall(event.peer_id, event.call_type);
    });
    Subscribe<EventOutgoingCall>(queue, [impl](auto const& event) {
      impl->OnOutgoingCall(event.peer_id, event.call_type);
    });
    Subscribe<EventCallRinging>(queue, [impl](auto const& event) { impl->OnCallRinging(event.peer_id); });
    Subscribe<EventCallAccepted>(queue, [impl](auto const& event) { impl->OnCallAccepted(event.peer_id); });
    Subscribe<EventCallTerminated>(queue, [impl](auto const& event) { impl->OnCallTerminated(event.peer_id); });
    Subscribe<EventMediaPeerConnectionStateChanged>(queue, [impl](auto const& event) {
      auto const state = event.state;
      if (state == transport::PeerConnectionState::closed || state == transport::PeerConnectionState::disconnected ||
          state == transport::PeerConnectionState::failed) {
        impl->OnCallTerminated(event.peer_id);
      } else if (state == transport::PeerConnectionState::connected) {
        impl->OnCallConnected(event.peer_id);
      }
    });
  }

  ~CallViewModel() {
    for (auto const id : eventUnsubscribers_) {
      EventQueue::Instance().Unsubscribe(id);
    }
    impl_->StopTicker();
  }

  [[nodiscard]] auto GetState() const -> CallState {
    return impl_->GetState();
  }

  void StartVoiceCall(ContactID const& peer_id) {
    impl_->StartVoiceCall(peer_id);
  }
  void StartVideoCall(ContactID const& peer_id) {
    impl_->StartVideoCall(peer_id);
  }
  void Hangup() {
    impl_->Hangup();
  }
  void AcceptCall() {
    impl_->AcceptCall();
  }
  void RejectCall() {
    impl_->RejectCall();
  }
  void ToggleMute() {
    impl_->ToggleMute();
  }
  void ToggleSpeaker() {
    impl_->ToggleSpeaker();
  }
  void StopCallMedia() {
    impl_->StopCallMedia();
  }

 private:
  struct Impl : std::enable_shared_from_this<Impl> {
    std::shared_ptr<Client> client;

    mutable std::mutex mutex;
    CallState state;
    DispatchQueue ticker_{"CallTicker", 1};
    std::atomic<bool> tick_active_{false};

    explicit Impl(std::shared_ptr<Client> client)
        : client(std::move(client)) {
      state = this->client->GetCurrentCallState();
    }

    auto GetState() const -> CallState {
      std::lock_guard<std::mutex> const lock(mutex);
      return state;
    }

    void Notify() const {
      std::lock_guard<std::mutex> const lock(mutex);
      EventQueue::Instance().Push(EventCallStateChanged{state});
    }

    void StartTicker() {
      if (tick_active_.exchange(true)) return;
      ticker_.DispatchAfter(std::chrono::seconds(1), [this] { Tick(); });
    }

    void StopTicker() {
      tick_active_ = false;
      ticker_.RemovePending();
    }

    void Tick() {
      if (!tick_active_) return;
      bool should_notify = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if (state.is_in_call) {
          state.duration_seconds++;
          should_notify = true;
        } else {
          tick_active_ = false;
        }
      }
      if (should_notify) Notify();
      if (tick_active_) {
        ticker_.DispatchAfter(std::chrono::seconds(1), [this] { Tick(); });
      }
    }

    void StartVoiceCall(ContactID const& peer_id) {
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state = {
            .is_connecting = true,
            .is_in_call = false,
            .is_ringing = false,
            .call_direction = messaging::CallDirection::outgoing,
            .peer_id = peer_id,
            .call_type = messaging::HistoryEntryKind::voiceCall,
            .is_muted = false,
            .is_speaker_on = false
        };
      }
      client->StartVoiceCall(peer_id);
      Notify();
    }

    void StartVideoCall(ContactID const& peer_id) {
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state = {
            .is_connecting = true,
            .is_in_call = false,
            .is_ringing = false,
            .call_direction = messaging::CallDirection::outgoing,
            .peer_id = peer_id,
            .call_type = messaging::HistoryEntryKind::videoCall,
            .is_muted = false,
            .is_speaker_on = false
        };
      }
      client->StartVideoCall(peer_id);
      Notify();
    }

    void Hangup() {
      StopTicker();
      ContactID peer_id_to_hangup;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        peer_id_to_hangup = state.peer_id;
        state = {};
      }
      if (!peer_id_to_hangup.IsEmpty()) {
        client->Hangup(peer_id_to_hangup, true);
      }
      Notify();
    }

    void AcceptCall() {
      ContactID peer_id;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        peer_id = state.peer_id;
        state.is_connecting = false;
        state.is_in_call = true;
        state.is_ringing = false;
        state.duration_seconds = 0;
      }
      if (!peer_id.IsEmpty()) {
        client->AcceptCall(peer_id);
      }
      Notify();
    }

    void RejectCall() {
      StopTicker();
      ContactID peer_id;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        peer_id = state.peer_id;
        state = {};
      }
      if (!peer_id.IsEmpty()) {
        client->RejectCall(peer_id);
      }
      Notify();
    }

    void ToggleMute() {
      std::lock_guard<std::mutex> const lock(mutex);
      state.is_muted = !state.is_muted;
      EventQueue::Instance().Push(EventCmdSetMicrophoneMute{state.is_muted});
      Notify();
    }

    void ToggleSpeaker() {
      std::lock_guard<std::mutex> const lock(mutex);
      state.is_speaker_on = !state.is_speaker_on;
      EventQueue::Instance().Push(EventCmdSetSpeakerphone{state.is_speaker_on});
      Notify();
    }

    void StopCallMedia() {
      std::lock_guard<std::mutex> const lock(mutex);
      state.is_muted = false;
      state.is_speaker_on = false;
      EventQueue::Instance().Push(EventCmdSetSpeakerphone{false});
      Notify();
    }

    void OnIncomingCall(ContactID const& peer_id, messaging::HistoryEntryKind call_type) {
      bool should_notify = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if (!state.is_connecting && !state.is_in_call && !state.is_ringing) {
          state.is_connecting = false;
          state.is_ringing = true;
          state.peer_id = peer_id;
          state.call_type = call_type;
          state.call_direction = messaging::CallDirection::incoming;
          should_notify = true;
        }
      }
      if (should_notify) {
        Notify();
        StartTicker();
      }
    }

    void OnOutgoingCall(ContactID const& peer_id, messaging::HistoryEntryKind call_type) {
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state = {
            .is_connecting = true,
            .is_in_call = false,
            .is_ringing = false,
            .call_direction = messaging::CallDirection::outgoing,
            .peer_id = peer_id,
            .call_type = call_type
        };
      }
      Notify();
    }

    void OnCallRinging(ContactID const& peer_id) {
      bool should_notify = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if (state.is_connecting && state.peer_id == peer_id) {
          state.is_connecting = false;
          state.is_ringing = true;
          should_notify = true;
        }
      }
      if (should_notify) {
        Notify();
      }
    }

    void OnCallAccepted(ContactID const& peer_id) {
      bool should_notify = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if ((state.is_connecting || state.is_ringing || !state.is_in_call) && state.peer_id == peer_id) {
          state.is_connecting = false;
          state.is_ringing = false;
          state.is_in_call = true;
          state.duration_seconds = 0;
          should_notify = true;
        }
      }
      if (should_notify) {
        Notify();
      }
    }

    void OnCallConnected(ContactID const& peer_id) {
      bool should_notify = false;
      // Wait for Accepted or Ringing updates from MediaCallService logic.
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if (!state.is_in_call && (state.is_connecting || state.is_ringing) && state.peer_id == peer_id &&
            state.call_direction == messaging::CallDirection::outgoing) {
          state.is_connecting = false;
          state.is_ringing = true;  // Connected -> Ringing (waiting for accept)
          should_notify = true;
        }
      }
      if (should_notify) {
        Notify();
      }
    }

    void OnCallTerminated(ContactID const& peer_id) {
      StopTicker();
      bool should_notify = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if ((state.is_connecting || state.is_in_call || state.is_ringing) && state.peer_id == peer_id) {
          state = {};
          should_notify = true;
        }
      }
      if (should_notify) {
        Notify();
      }
    }
  };

  std::shared_ptr<Impl> impl_;

  std::vector<EventQueue::SubscriberTypeId> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.push_back(id);
  }
};

}  // namespace eillance::view_model
