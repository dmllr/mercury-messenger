#include "call_view_model.hpp"

namespace eillance::view_model {

struct CallViewModel::Impl : std::enable_shared_from_this<Impl> {
  std::shared_ptr<Client> client;
  std::shared_ptr<ClientEventRouter> router;
  std::shared_ptr<Observer> observer;

  mutable std::mutex mutex;
  CallState state;

  Impl(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : client(std::move(client)),
        router(std::move(router)) {}

  void SetObserver(std::shared_ptr<Observer> obs) {
    std::lock_guard<std::mutex> const lock(mutex);
    observer = std::move(obs);
    if (observer) {
      observer->OnCallStateChanged(state);
    }
  }

  auto GetState() const -> CallState {
    std::lock_guard<std::mutex> const lock(mutex);
    return state;
  }

  void Notify() const {
    std::lock_guard<std::mutex> const lock(mutex);
    if (observer) {
      observer->OnCallStateChanged(state);
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
          .call_type = messaging::HistoryEntryKind::voiceCall
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
          .call_type = messaging::HistoryEntryKind::videoCall
      };
    }
    client->StartVideoCall(peer_id);
    Notify();
  }

  void Hangup() {
    ContactID peer_id_to_hangup;
    {
      std::lock_guard<std::mutex> const lock(mutex);
      if (!state.is_connecting && !state.is_in_call && !state.is_ringing) {
        return;
      }
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
    auto call_kind = messaging::HistoryEntryKind::voiceCall;
    {
      std::lock_guard<std::mutex> const lock(mutex);
      if (!state.is_ringing) {
        return;
      }
      peer_id = state.peer_id;
      call_kind = state.call_type;
      state.is_connecting = false;
      state.is_in_call = true;
      state.is_ringing = false;
    }
    if (!peer_id.IsEmpty()) {
      client->AcceptCall(peer_id, call_kind);
    }
    Notify();
  }

  void RejectCall() {
    ContactID peer_id;
    {
      std::lock_guard<std::mutex> const lock(mutex);
      if (!state.is_ringing) {
        return;
      }
      peer_id = state.peer_id;
      state = {};
    }
    if (!peer_id.IsEmpty()) {
      client->RejectCall(peer_id);
    }
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
    }
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
      if ((state.is_connecting || state.is_ringing) && state.peer_id == peer_id) {
        state.is_connecting = false;
        state.is_ringing = false;
        state.is_in_call = true;
        should_notify = true;
      }
    }
    if (should_notify) {
      Notify();
    }
  }

  void OnCallTerminated(ContactID const& peer_id) {
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

CallViewModel::CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : impl_(std::make_shared<Impl>(std::move(client), std::move(router))) {
  auto& queue = impl_->router->Queue();
  auto impl = impl_;
  Subscribe<EventIncomingCall>(queue, [impl](auto const& event) {
    impl->OnIncomingCall(event.peer_id, event.call_type);
  });
  Subscribe<EventCallRinging>(queue, [impl](auto const& event) { impl->OnCallRinging(event.peer_id); });
  Subscribe<EventCallAccepted>(queue, [impl](auto const& event) { impl->OnCallAccepted(event.peer_id); });
  Subscribe<EventCallTerminated>(queue, [impl](auto const& event) { impl->OnCallTerminated(event.peer_id); });
  Subscribe<EventStatusChanged>(queue, [impl](auto const& event) {
    if (event.source == EventSource::peerConnectionState) {
      auto const state = static_cast<transport::PeerConnectionState>(event.state);
      if (state == transport::PeerConnectionState::closed || state == transport::PeerConnectionState::disconnected ||
          state == transport::PeerConnectionState::failed) {
        impl->OnCallTerminated(event.peer_id);
      }
    }
  });
}

CallViewModel::~CallViewModel() {
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
}

template <typename EventType>
void CallViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
  auto event_id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, event_id] { queue.Unsubscribe<EventType>(event_id); });
}

void CallViewModel::SetObserver(std::shared_ptr<Observer> observer) const { impl_->SetObserver(std::move(observer)); }

auto CallViewModel::GetState() const -> CallState { return impl_->GetState(); }

void CallViewModel::StartVoiceCall(ContactID const& peer_id) { impl_->StartVoiceCall(peer_id); }
void CallViewModel::StartVideoCall(ContactID const& peer_id) { impl_->StartVideoCall(peer_id); }
void CallViewModel::Hangup() { impl_->Hangup(); }
void CallViewModel::AcceptCall() { impl_->AcceptCall(); }
void CallViewModel::RejectCall() { impl_->RejectCall(); }

}  // namespace eillance::view_model
