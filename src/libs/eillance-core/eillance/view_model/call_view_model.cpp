#include "call_view_model.hpp"

namespace eillance::view_model {

CallViewModel::CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)), router_(std::move(router)) {
  auto& queue = router_->Queue();
  Subscribe<EventIncomingCall>(queue, [this](const auto& event) { OnIncomingCall(event.peer_id, event.call_type); });
  Subscribe<EventCallRinging>(queue, [this](const auto& event) { OnCallRinging(event.peer_id); });
  Subscribe<EventCallAccepted>(queue, [this](const auto& event) { OnCallAccepted(event.peer_id); });
  Subscribe<EventCallTerminated>(queue, [this](const auto& event) { OnCallTerminated(event.peer_id); });
  Subscribe<EventStatusChanged>(queue, [this](const auto& event) {
    if (event.source == EventSource::peerConnectionState) {
      const auto state = static_cast<transport::PeerConnectionState>(event.state);
      if (state == transport::PeerConnectionState::Closed ||
          state == transport::PeerConnectionState::Disconnected ||
          state == transport::PeerConnectionState::Failed) {
        OnCallTerminated(event.peer_id);
      }
    }
  });
}

CallViewModel::~CallViewModel() {
  for (const auto& unsubscribe: eventUnsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void CallViewModel::Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
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

void CallViewModel::StartVoiceCall(const ContactID& peerId) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peerId,
        .call_type = messaging::HistoryEntryKind::voiceCall};
  }
  client_->StartVoiceCall(peerId);
  Notify();
}

void CallViewModel::StartVideoCall(const ContactID& peerId) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_ = {
        .is_connecting = true,
        .is_in_call = false,
        .is_ringing = false,
        .call_direction = messaging::CallDirection::outgoing,
        .peer_id = peerId,
        .call_type = messaging::HistoryEntryKind::videoCall};
  }
  client_->StartVideoCall(peerId);
  Notify();
}

void CallViewModel::Hangup() {
  ContactID peer_id_to_hangup;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) { return; }
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
  messaging::HistoryEntryKind call_kind = messaging::HistoryEntryKind::voiceCall;
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

void CallViewModel::OnIncomingCall(const ContactID& peerId, messaging::HistoryEntryKind callType) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (!state_.is_connecting && !state_.is_in_call && !state_.is_ringing) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    state_.peer_id = peerId;
    state_.call_type = callType;
    state_.call_direction = messaging::CallDirection::incoming;
    Notify();
  }
}

void CallViewModel::OnCallRinging(const ContactID& peerId) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_.is_connecting && state_.peer_id == peerId) {
    state_.is_connecting = false;
    state_.is_ringing = true;
    Notify();
  }
}

void CallViewModel::OnCallAccepted(const ContactID& peerId) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if ((state_.is_connecting || state_.is_ringing) && state_.peer_id == peerId) {
    state_.is_connecting = false;
    state_.is_ringing = false;
    state_.is_in_call = true;
    Notify();
  }
}

void CallViewModel::OnCallTerminated(const ContactID& peerId) {
  const std::lock_guard<std::mutex> lock(mutex_);
  if ((state_.is_connecting || state_.is_in_call || state_.is_ringing) && state_.peer_id == peerId) {
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
