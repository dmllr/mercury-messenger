#include "call_view_model.hpp"

namespace eillance::view_model {

CallViewModel::CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)),
      router_(std::move(router)) {
  auto& queue = router_->Queue();
  Subscribe<EventIncomingCall>(queue, [this](auto const& event) { OnIncomingCall(event.peer_id, event.call_type); });
  Subscribe<EventCallRinging>(queue, [this](auto const& event) { OnCallRinging(event.peer_id); });
  Subscribe<EventCallAccepted>(queue, [this](auto const& event) { OnCallAccepted(event.peer_id); });
  Subscribe<EventCallTerminated>(queue, [this](auto const& event) { OnCallTerminated(event.peer_id); });
  Subscribe<EventStatusChanged>(queue, [this](auto const& event) {
    if (event.source == EventSource::peerConnectionState) {
      auto const state = static_cast<transport::PeerConnectionState>(event.state);
      if (state == transport::PeerConnectionState::closed || state == transport::PeerConnectionState::disconnected ||
          state == transport::PeerConnectionState::failed) {
        OnCallTerminated(event.peer_id);
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

void CallViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  std::lock_guard<std::mutex> const lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnCallStateChanged(state_);
  }
}

auto CallViewModel::GetState() const -> CallState {
  std::lock_guard<std::mutex> const lock(mutex_);
  return state_;
}

void CallViewModel::StartVoiceCall(ContactID const& peer_id) {
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::voiceCall
    };
  }
  client_->StartVoiceCall(peer_id);
  Notify();
}

void CallViewModel::StartVideoCall(ContactID const& peer_id) {
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::videoCall
    };
  }
  client_->StartVideoCall(peer_id);
  Notify();
}

void CallViewModel::Hangup() {
  ContactID peer_id_to_hangup;
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) {
      return;
    }
    peer_id_to_hangup = state_.peer_id;
    state_ = {};
  }
  if (!peer_id_to_hangup.IsEmpty()) {
    client_->Hangup(peer_id_to_hangup, true);
  }
  Notify();
}

void CallViewModel::AcceptCall() {
  ContactID peer_id;
  auto call_kind = messaging::HistoryEntryKind::voiceCall;
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (!state_.is_ringing) {
      return;
    }
    peer_id = state_.peer_id;
    call_kind = state_.call_type;
    state_.is_connecting = false;
    state_.is_in_call = true;
    state_.is_ringing = false;
  }
  if (!peer_id.IsEmpty()) {
    client_->AcceptCall(peer_id, call_kind);
  }
  Notify();
}

void CallViewModel::RejectCall() {
  ContactID peer_id;
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    if (!state_.is_ringing) {
      return;
    }
    peer_id = state_.peer_id;
    state_ = {};
  }
  if (!peer_id.IsEmpty()) {
    client_->RejectCall(peer_id);
  }
  Notify();
}

void CallViewModel::OnIncomingCall(ContactID const& peer_id, messaging::HistoryEntryKind call_type) {
  std::lock_guard<std::mutex> const lock(mutex_);
  if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    state_.peer_id = peer_id;
    state_.call_type = call_type;
    state_.call_direction = messaging::CallDirection::incoming;
    Notify();
  }
}

void CallViewModel::OnCallRinging(ContactID const& peer_id) {
  std::lock_guard<std::mutex> const lock(mutex_);
  if (state_.is_connecting && state_.peer_id == peer_id) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    Notify();
  }
}

void CallViewModel::OnCallAccepted(ContactID const& peer_id) {
  std::lock_guard<std::mutex> const lock(mutex_);
  if ((state_.is_connecting || state_.is_ringing) && state_.peer_id == peer_id) {
    state_.is_connecting = false;
    state_.is_ringing = false;
    state_.is_in_call = true;
    Notify();
  }
}

void CallViewModel::OnCallTerminated(ContactID const& peer_id) {
  std::lock_guard<std::mutex> const lock(mutex_);
  if ((state_.is_connecting || state_.is_in_call || state_.is_ringing) && state_.peer_id == peer_id) {
    state_ = {};
    Notify();
  }
}

void CallViewModel::Notify() const {
  if (observer_) {
    observer_->OnCallStateChanged(state_);
  }
}

}  // namespace eillance::view_model
