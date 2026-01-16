#include "call_view_model.hpp"

namespace eillance::view_model {

CallViewModel::CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)), router_(std::move(router)) {
  auto& q = router_->Queue();
  Subscribe<EventIncomingCall>(q, [this](const auto& e) { OnIncomingCall(e.peer_id, e.call_type); });
  Subscribe<EventCallRinging>(q, [this](const auto& e) { OnCallRinging(e.peer_id); });
  Subscribe<EventCallAccepted>(q, [this](const auto& e) { OnCallAccepted(e.peer_id); });
  Subscribe<EventCallTerminated>(q, [this](const auto& e) { OnCallTerminated(e.peer_id); });
  Subscribe<EventStatusChanged>(q, [this](const auto& e) {
    if (e.source == EventSource::peerConnectionState) {
      auto state = static_cast<transport::PeerConnectionState>(e.state);
      if (state == transport::PeerConnectionState::Closed ||
          state == transport::PeerConnectionState::Disconnected ||
          state == transport::PeerConnectionState::Failed) {
        OnCallTerminated(e.peer_id);
      }
    }
  });
}

CallViewModel::~CallViewModel() {
  for (const auto& unsubscribe: event_unsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void CallViewModel::Subscribe(EventQueue& q, std::function<void(const EventType&)> callback) {
  auto id = q.Subscribe<EventType>(std::move(callback));
  event_unsubscribers_.push_back([&q, id] { q.Unsubscribe<EventType>(id); });
}

void CallViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  const std::lock_guard<std::mutex> lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnCallStateChanged(state_);
  }
}

auto CallViewModel::GetState() const -> CallState {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void CallViewModel::StartVoiceCall(const ContactID& peer_id) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::voiceCall};
  }
  client_->StartVoiceCall(peer_id);
  Notify();
}

void CallViewModel::StartVideoCall(const ContactID& peer_id) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peer_id,
        .call_type = messaging::HistoryEntryKind::videoCall};
  }
  client_->StartVideoCall(peer_id);
  Notify();
}

void CallViewModel::Hangup() {
  ContactID peer_id_to_hangup;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) return;
    peer_id_to_hangup = state_.peer_id;
    state_ = {};
  }
  if (!peer_id_to_hangup.IsEmpty()) {
    client_->Hangup(peer_id_to_hangup);
  }
  Notify();
}

void CallViewModel::AcceptCall() {
  ContactID peer_id;
  messaging::HistoryEntryKind call_kind;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
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
    const std::lock_guard<std::mutex> lock(mutex_);
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

void CallViewModel::OnIncomingCall(const ContactID& peer_id, messaging::HistoryEntryKind call_type) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    state_.peer_id = peer_id;
    state_.call_type = call_type;
    state_.call_direction = messaging::CallDirection::incoming;
    Notify();
  }
}

void CallViewModel::OnCallRinging(const ContactID& peer_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_.is_connecting && state_.peer_id == peer_id) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    Notify();
  }
}

void CallViewModel::OnCallAccepted(const ContactID& peer_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if ((state_.is_connecting || state_.is_ringing) && state_.peer_id == peer_id) {
    state_.is_connecting = false;
    state_.is_ringing = false;
    state_.is_in_call = true;
    Notify();
  }
}

void CallViewModel::OnCallTerminated(const ContactID& peer_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if ((state_.is_connecting || state_.is_in_call || state_.is_ringing) && state_.peer_id == peer_id) {
    state_ = {};
    Notify();
  }
}

void CallViewModel::Notify() {
  if (observer_) {
    observer_->OnCallStateChanged(state_);
  }
}

}  // namespace eillance::view_model
